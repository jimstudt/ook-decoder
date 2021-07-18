#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

#include "ook.h"

#define ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL  0x31
#define ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY     0x38

// From draythomp/Desert-home-rtl_433
// matches acu-link internet bridge values
// The mapping isn't circular, it jumps around.
// units are 22.5 deg
int const acurite_5n1_winddirections[] = {
    14, // 0 - NW
    11, // 1 - WSW
    13, // 2 - WNW
    12, // 3 - W
    15, // 4 - NNW
    10, // 5 - SW
    0,   // 6 - N
    9, // 7 - SSW
    3,  // 8 - ENE
    6, // 9 - SE
    4,  // a - E
    5, // b - ESE
    2,  // c - NE
    7, // d - SSE
    1,  // e - NNE
    8, // f - S
};

int verbose=0;

struct report {
    uint32_t valid:1;
    uint32_t channel:2;
    uint32_t id:14;
    uint32_t temperature:12;
    uint32_t humidity:8;
    uint32_t batteryLow:1;
    uint32_t windValid:1;
    uint32_t wind10:10;   // m/s * 10
    uint32_t direction:4; // 22.5 deg increments
    uint32_t rainValid:1;
    uint32_t rain:14;     // mm
};
static const uint8_t chanMap[] = { 3, 0, 2, 1};
		

static bool isStart( uint32_t high_ns, uint32_t low_ns) {
    return high_ns >= 600000 && high_ns <= 700000 && low_ns >= 500000 && low_ns <= 600000;
}
static bool isOne( uint32_t high_ns, uint32_t low_ns) {
    return high_ns >= 400000 && high_ns <= 500000 && low_ns >= 100000 && low_ns <= 220000;
}
static bool isZero( uint32_t high_ns, uint32_t low_ns) {
    return high_ns >= 200000 && high_ns <= 300000 && low_ns >= 300000 && low_ns <= 400000;
}
static bool isStop( uint32_t high_ns, uint32_t low_ns) {
    return high_ns >= 200000 && high_ns <= 300000 && low_ns >= 500000;
}

static struct report decode_acurite( const struct ook_burst *burst) {
    enum { IDLE=0, STARTS, CONTENT } state = IDLE;
    unsigned idleGarbage = 0;
    unsigned falseStarts = 0;

    uint8_t data[8];
    uint32_t bits = 0;
    
    for ( uint32_t p = 0; p < burst->pulses; p++) {
	switch( state) {
	  case IDLE:
	    if ( isStart( burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds) ) {
		state = STARTS;
	    } else {
		idleGarbage++;
	    }
	    break;
	  case STARTS:
	    if ( isStart(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) continue;
	    if ( !isOne(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds) &&
		 !isZero(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) {
		state = IDLE;
		falseStarts++;
		continue;
	    }
	    state = CONTENT;
	    bits = 0;
	    memset( data, 0, sizeof data);
	    
	    // FALLTHROUGH!!!!!!!
	  case CONTENT:
	    if ( isOne(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds) ) {
		if ( bits < 8*sizeof(data)) {
		    uint8_t byte = bits/8;
		    uint8_t bit = 1 << (7 - bits%8);
		    
		    data[byte] |= bit;
		}
		bits++;
	    } else if ( isZero(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) {
		bits++;
	    } else if ( isStop(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) {
		if (verbose) {
		    fprintf(stderr, "At stop we have %d bits, ", bits);
		    for ( uint8_t i = 0; i < bits/8; i++) fprintf(stderr, "%02x", data[i]);
		    fprintf(stderr, "\n");
		}
		
		state = IDLE;

		if ( bits % 8 != 0 ) {
		    if (verbose) fprintf(stderr, "Not an integral number of bytes: %d bits\n", bits);
		    continue;
		}
		if ( bits > 8 * sizeof(data)) {
		    if (verbose) fprintf(stderr, "Bits overran buffer: %d bits\n", bits);
		    continue;
		}

		uint8_t sum = 0;
		for ( uint8_t i = 0; i < bits/8 - 1; i++) sum += data[i]; 
		
		if ( sum != data[ bits/8 - 1 ] ) {
		    if (verbose) fprintf(stderr, "CRC invalid: %02x != %02x\n", sum, data[ bits/8 -1]);
		    continue;
		}

		if ( bits < 56) {
		    if ( verbose) fprintf(stderr, "Too short to be an acurite message: %d bits\n", bits);
		    continue;
		}

		uint8_t battery = (data[2]>>6) & 1;
		uint8_t message = (data[2]) & 0x3f;
		uint8_t mParity = (data[2]>>7) & 1;
		
		if ( mParity != ( (__builtin_popcount( message) + battery) & 1)) {
		    if ( verbose) fprintf(stderr, "parity error in message code\n");
		    continue;
		}

		switch( message) {
		  case 4:       // 592TXR
		      {
			  if ( bits != 56) {
			      if ( verbose) fprintf(stderr, "592TXR message is not 56 bits\n");
			      continue;
			  }

			  uint8_t channel = chanMap[ (data[0]>>6) & 0x03];
			  uint16_t id = (( data[0] & 0x3f)<<8) | data[1];

			  uint8_t humidity = data[3] & 0x7f;
			  uint8_t hParity = (data[3]>>7) & 1;
			  if ( hParity != (__builtin_popcount( humidity) & 1)) {
			      if ( verbose) fprintf(stderr, "parity error in humidity\n");
			      continue;
			  }

			  uint8_t tempHigh = data[4] & 0x7f;
			  uint8_t tempLow = data[5] & 0x7f;
			  uint16_t temperatureRaw = (tempHigh<<7) | tempLow;
			  uint16_t temperature10 = temperatureRaw - 1000;
			  uint8_t tHighParity = (data[4]>>7) & 1;
			  uint8_t tLowParity = (data[5]>>7) & 1;

			  if ( tHighParity != (__builtin_popcount( tempHigh) & 1) ||
			       tLowParity != (__builtin_popcount( tempLow) & 1)) {
			      if ( verbose) fprintf(stderr, "parity error in temperature\n");
			      continue;
			  }
			  if ( verbose) fprintf(stderr, "  chan=%d id=%d bat=%d msg=%d temperature=%.1f hum=%d\n", channel, id, battery, message, temperature10/10.0,  humidity);
			  struct report good = { .valid = 1,
						 .channel = channel,
						 .id = id,
						 .batteryLow = !battery,
						 .temperature = temperature10,
						 .humidity = humidity};
			  return good;
		      }
		  case ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY:
		  case ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL:
		      {
			  if ( bits != 64) {
			      if ( verbose) fprintf(stderr, "5-n-1 message is not 64 bits\n");
			      continue;
			  }

			  uint8_t channel = chanMap[ (data[0]>>6) & 0x03];
			  // uint8_t resend = (data[0])>>4 & 0x03; 
			  uint16_t id = (( data[0] & 0x0f)<<8) | data[1];

			  uint16_t pulsesPerFourSeconds = ((data[3]<<3) & 0xf8) | ((data[4]>>4) & 0x07);
			  float windKmPerHour = pulsesPerFourSeconds * 0.8278 + 1.00;
			  float windSpeedMetersPerSecond = pulsesPerFourSeconds == 0 ? 0.0 : windKmPerHour * 0.27778;

			  static bool stashedTemperatureValid = false;
			  static float stashedTemperature = 0;
			  static uint8_t stashedHumidity = 0;

			  static bool stashedDirectionValid = false;
			  static uint8_t stashedDirection = 0;
			  static uint16_t stashedRain = 0;
			  
			  switch (message) {
			    case ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY:
				{
				    uint16_t tempRaw = ((data[4]<<7) & 0x780) | (data[5] & 0x7F);
				    float temperatureF = (tempRaw - 400) * 0.1;
				    float temperature = (temperatureF - 32.0)*(100.0/180.0);
					
				    uint8_t humidity = (data[6] & 0x7f);
				    if ( verbose) fprintf(stderr, "  msg=%d chan=%d id=%d wind speed = %.1fm/s temp=%.1f hum=%d\n", message, channel, id, windSpeedMetersPerSecond, temperature, humidity);

				    stashedTemperatureValid = true;
				    stashedTemperature = temperature;
				    stashedHumidity = humidity;

				    if ( stashedDirectionValid) {
					struct report good = { .valid = 1,
							       .channel = channel,
							       .id = id,
							       .batteryLow = !battery,
							       .temperature = temperature*10.0,
							       .humidity = humidity,
							       .windValid = true,
							       .wind10 = windSpeedMetersPerSecond * 10.0,
							       .direction = stashedDirection,
							       .rainValid = true,
							       .rain = stashedRain,
					};
					stashedDirectionValid = false;  // we used it
					return good;
				    } else {
					continue;
				    }
				}
			    case ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL:
				{
				    uint8_t direction = acurite_5n1_winddirections[ data[4] & 0x0f];
				    float rainInches = ( ((data[5]<<7)&0x3f80) & (data[6] & 0x7f) ) / 100.0;
				    float rainmm = rainInches * 25.4;

				    stashedDirectionValid = true;
				    stashedDirection = direction;
				    stashedRain = rainmm;
				    
				    if ( verbose) fprintf(stderr, "  msg=%d chan=%d id=%d wind speed = %.1fm/s windDir=%.1fdeg rain=%.1fmm\n", message, channel, id, windSpeedMetersPerSecond, direction*22.5, rainmm);
				    if ( stashedTemperatureValid) {
					struct report good = { .valid = 1,
							       .channel = channel,
							       .id = id,
							       .batteryLow = !battery,
							       .temperature = stashedTemperature*10.0,
							       .humidity = stashedHumidity,
							       .windValid = true,
							       .wind10 = windSpeedMetersPerSecond * 10.0,
							       .direction = direction,
							       .rainValid = true,
							       .rain = rainmm,
					};
					stashedTemperatureValid = false;  // we used it
					return good;
				    } else {
					continue;
				    }
				}
			    default:
			      fprintf(stderr,"5n1 got stupid message: %d\n", message);
			      continue;
			  }
			  continue;
		      }
		  default:
		    if ( verbose) fprintf(stderr, "Unknown acurite message code: %d\n", message);
		    continue;
		}
	    } else {
		if ( verbose) fprintf(stderr, "Message fell apart in content.\n");
		state = IDLE;
	    }
	}
    }

    struct report bad = { .valid = 0 };
    return bad;
}

static void writeReport( const struct report *report, const char *template) {
    char fn[1024];
    char fnTemp[1025];

    snprintf( fn, sizeof fn, "%s-%d-%d.json", template, report->channel, report->id);
    snprintf( fnTemp, sizeof fnTemp, "%s,", fn);

    FILE *f = fopen( fnTemp, "w");
    if ( !f) {
	fprintf(stderr,"Failed to make temp file '%s': %s\n", fnTemp, strerror(errno));
	return;
    }
    
    fprintf(f,"{\n");
    fprintf(f,"\t\"channel\":%d,\n", report->channel);
    fprintf(f,"\t\"id\":%d,\n", report->id);
    fprintf(f,"\t\"temperature\":%.1f,\n", report->temperature / 10.0);
    fprintf(f,"\t\"humidity\":%d,\n", report->humidity);
    if ( report->windValid) {
	fprintf(f,"\t\"windspeed\":%.1f,\n", report->wind10/10.0);
	fprintf(f,"\t\"windbearing\":%.1f,\n", report->direction*22.5);
    }
    if ( report->rainValid) {
	fprintf(f,"\t\"rainfall\":%d,\n", report->rain);
    }
    fprintf(f,"\t\"batteryLow\":%d\n", report->batteryLow);
    fprintf(f,"}\n");
    fclose(f);

    if (rename( fnTemp, fn)) {
	fprintf(stderr,"Failed to rename temp file: %s\n", strerror(errno));
	unlink(fnTemp);
    }
}


static void showHelp( FILE *f)
{
    fprintf(f, 
	    "Usage: acurite [-h] [-?] [-v] [-a mcastaddr] [-p mcastport] [-i mcastinterface]\n"
	    "  -h | -? | --help                      display usage and exit\n"
	    "  -v | --verbose                        verbose logging\n"
	    "  -a addr | --multicast-address addr    multicast address, default 236.0.0.1\n"
	    "  -p port | --multicast-port port       multicast port, default 3636\n"
	    "  -i addr | --multicast-interface addr  address of the multicast interface, default 127.0.0.1\n"
	    "  -r path | --recent path               path to most recent data, /tmp/current-weather, appends channel, identifier, and .json.\n"
	    );
}


int main( int argc, char **argv)
{
    const char *multicastAddress = "236.0.0.1";
    const char *multicastPort = "3636";
    const char *multicastInterface = "127.0.0.1";
    const char *recentFileName = "/tmp/current-weather";

    // Handle options
    for(;;) {
	int optionIndex = 0;
	static struct option options[] = {
	    { "verbose", no_argument, 0, 'v' },
	    { "help",    no_argument, 0, 'h' },
	    { "multicast-address", required_argument, 0, 'a'},
	    { "multicast-port", required_argument, 0, 'p' },
	    { "multicast-interface", required_argument, 0, 'i' },
	    { "recent", required_argument, 0, 'r' },
	    { 0,0,0,0}
	};

	int c = getopt_long( argc, argv, "vh?f:a:p:i:r:", options, &optionIndex );
	if ( c == -1) break;

	switch(c) {
	  case 'h':
	  case '?':
	    showHelp(stdout);
	    return 0;
	  case 'v':
	    verbose = 1;
	    break;
	  case 'a':
	    multicastAddress = optarg;
	    break;
	  case 'p':
	    multicastPort = optarg;
	    break;
	  case 'i':
	    multicastInterface = optarg;
	    break;
	  case 'r':
	    recentFileName = optarg;
	    break;
	  default:
	    fprintf(stderr,"Illegal option\n");
	    showHelp(stderr);
	    exit(1);
	}
    }

    if ( verbose) fprintf(stderr,"Recent file is %s\n", recentFileName);

    // Parse our multicast address
    int sock = ook_open( multicastAddress, multicastPort, multicastInterface);
    if ( sock < 0) {
	fprintf(stderr,"Failed to open multicast interface\n");
	exit(1);
    }

    for (;;) {
	struct ook_burst *burst;
	struct sockaddr_storage addr;
	socklen_t addrLen = sizeof(addr);

	int e = ook_decode_from_socket( sock, &burst, (struct sockaddr *)&addr, &addrLen, verbose);
	if ( e < 0) {
	    fprintf(stderr,"Failed to decode from socket: %s\n", strerror(errno));
	    break;
	}
	if ( e == 0) {
	    fprintf(stderr,"Corrupt burst\n");
	    continue;
	}

	if ( verbose) fprintf(stderr, "Considering a %u pulse burst...\n", burst->pulses);
	struct report r = decode_acurite( burst);

	if ( r.valid) {
	    writeReport( &r, recentFileName);
	}
	
	fflush(stdin);
	free(burst);
    }

    close(sock);
    return 0;
}
