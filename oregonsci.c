#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

#include "ook.h"

int verbose=0;

// Datum keeps enough information to sum them up and still compute the standard deviation
struct datum {
    unsigned n;
    double sum;
    double sumOfSquares;
    double maximum;
    double minimum;
};

static time_t oldestDatum = 0;
static struct datum temperature;
static struct datum humidity;
static struct datum averageWindSpeed;
static struct datum gustWindSpeed;
static struct datum rainfall;
static struct datum batteryLow;
static struct datum windDirection;

static void resetDatum(struct datum *d)
{
    memset(d,0,sizeof(*d));
}

static void addSample( struct datum *d, double v)
{
    if ( oldestDatum == 0) oldestDatum = time(0);

    if ( d->n==0) {
	d->minimum = v;
	d->maximum = v;
    } else {
	if ( v > d->maximum) d->maximum = v;
	if ( v < d->minimum) d->minimum = v;
    }
    d->n++;
    d->sum += v;
    d->sumOfSquares += v*v;
}

static void dumpDatum( struct datum *d, const char *name, const char *units)
{
    if ( d->n == 0) {
	fprintf(stderr,"%s no data\n", name);
    } else {
	fprintf(stderr, "%s %u samples, %5.1f %s   min %5.1f  max %5.1f\n",
		name, d->n, d->sum/d->n, units, d->minimum, d->maximum);
    }
}

static void dumpWeather( void)
{
    dumpDatum( &temperature, "temperature", "C");
    dumpDatum( &humidity, "humidity", "%");
    dumpDatum( &averageWindSpeed, "wind", "m/s");
    dumpDatum( &gustWindSpeed, "gust", "m/s");
    dumpDatum( &rainfall, "rainfall", "m");
    dumpDatum( &batteryLow, "batt", "??");
    dumpDatum( &windDirection, "dir", "NEWS");
}

static void recordRecent( const char *file, double temp, double hum, double avgWind, double gustWind, 
			  double rain,
			  int batteryLowBits, int windDirectionBits)
{
    char name[256];
    strcpy(name,"/tmp/oregonsci.XXXXXX");

    int fd = mkstemp(name);
    if ( fd < 0) {
	fprintf(stderr,"Failed to create temp file for recent: %s\n", strerror(errno));
	return;
    }

    if ( fchmod( fd, 0664) != 0) {
	fprintf(stderr, "Failed to chmod temp file: %s\n", strerror(errno));
	unlink(name);
	close(fd);
	return;
    }

    FILE *f = fdopen(fd,"w");
    fprintf(f,"{\n");
    fprintf(f,"\t\"temperature\":%.1f,\n", temp);
    fprintf(f,"\t\"humidity\":%.1f,\n", hum);
    fprintf(f,"\t\"avgWindSpeed\":%.1f,\n", avgWind);
    fprintf(f,"\t\"gustSpeed\":%.1f,\n", gustWind);
    fprintf(f,"\t\"rainfall\":%.4f,\n", rain);
    fprintf(f,"\t\"batteryLow\":%d,\n", batteryLowBits);
    fprintf(f,"\t\"windDirection\":%d\n", windDirectionBits*45);
    fprintf(f,"}\n");
    fclose(f);

    if (rename( name, file)) {
	fprintf(stderr,"Failed to rename temp file: %s\n", strerror(errno));
    }
}

static void recordDatum( FILE *f, struct datum *d, const char *name, int comma)
{
    fprintf(f, "\t\"%s\" : { \"n\":%d, \"sum\":%.1f, \"sum2\":%.1f, \"min\":%.1f, \"max\":%.1f }%s\n",
	    name, d->n, d->sum, d->sumOfSquares, d->minimum, d->maximum, (comma ? ",":""));
    resetDatum( d);
}


static void recordPeriodic( const char *file)
{
    char name[256];
    strcpy(name,"/tmp/oregonsci.XXXXXX");

    int fd = mkstemp(name);
    if ( fd < 0) {
	fprintf(stderr,"Failed to create temp file for periodic: %s\n", strerror(errno));
	return;
    }

    if ( fchmod( fd, 0664) != 0) {
	fprintf(stderr, "Failed to chmod temp file: %s\n", strerror(errno));
	unlink(name);
	close(fd);
	return;
    }

    FILE *f = fdopen(fd,"w");
    fprintf(f,"{\n");
    fprintf(f,"\t\"start\":%lld,\n", (long long)oldestDatum);
    fprintf(f,"\t\"end\":%lld,\n", (long long)time(0));
    recordDatum( f, &temperature, "temperature", 1);
    recordDatum( f, &humidity, "humidity", 1);
    recordDatum( f, &averageWindSpeed, "avgWindSpeed", 1);
    recordDatum( f, &gustWindSpeed, "gustSpeed", 1);
    recordDatum( f, &rainfall, "rainfall", 1);
    recordDatum( f, &batteryLow, "batteryLow", 1);
    recordDatum( f, &windDirection, "windDirection", 0);
    fprintf(f,"}\n");
    fclose(f);

    char stamp[32]={0};
    strftime( stamp, sizeof(stamp), "%Y%m%d-%H%M%S", gmtime(&oldestDatum));
    char finalName[1024]="/ERROR";
    snprintf(finalName, sizeof(finalName)-1, "%s-%s.json", file, stamp);

    if (rename( name, finalName)) {
	fprintf(stderr,"Failed to rename temp file: %s\n", strerror(errno));
    }

    oldestDatum = 0;
   
}

static void showHelp( FILE *f)
{
    fprintf(f, 
	    "Usage: oregonsci [-h] [-?] [-v] [-a mcastaddr] [-p mcastport] [-i mcastinterface]\n"
	    "  -h | -? | --help                      display usage and exit\n"
	    "  -v | --verbose                        verbose logging\n"
	    "  -a addr | --multicast-address addr    multicast address, default 236.0.0.1\n"
	    "  -p port | --multicast-port port       multicast port, default 3636\n"
	    "  -i addr | --multicast-interface addr  address of the multicast interface, default 127.0.0.1\n"
	    "  -r path | --recent path               path to most recent data, /tmp/current-weather.json\n"
	    "  -P path | --periodic path             path to the periodic data, /tmp/weather\n"
	    "                                        timestamp.json gets appended.\n"
	    "  -m period | --minutes period          number of minutes between periodic data files.\n"
	    );
}

static int okChecksum( unsigned char *nibbles, unsigned int csumLocation) {
    unsigned int sum = 0;
    for ( int n = 7; n < csumLocation; n++) {  // skips sync and preamble
	sum += nibbles[n];
    }
    unsigned int csum = nibbles[csumLocation+1]*16 + nibbles[csumLocation];
    return sum == csum;
}

int main( int argc, char **argv)
{
    const char *multicastAddress = "236.0.0.1";
    const char *multicastPort = "3636";
    const char *multicastInterface = "127.0.0.1";
    const char *recentFileName = "/tmp/current-weather.json";
    const char *periodicFileName = "/tmp/weather";
    int minutes = 5;

    double recentTemp = -500.0;
    double recentHum = -1;
    double recentWind = -1;
    double recentGust = -1;
    double recentRain = -1;
    int recentBattery = 0;
    int recentDirection = -1;

    int lastRainCounter = -1;

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
	    { "periodic", required_argument, 0, 'P' },
	    { "minutes", required_argument, 0, 'm' },
	    { 0,0,0,0}
	};

	int c = getopt_long( argc, argv, "vh?f:a:p:i:m:r:P:", options, &optionIndex );
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
	  case 'P':
	    periodicFileName = optarg;
	    break;
	  case 'm':
	      {
		  int m = atoi(optarg);
		  if (m<1) {
		      fprintf(stderr,"Illegal minutes, less than 1\n");
		      exit(1);
		  }
		  minutes = m;
	      }
	      break;
	  default:
	    fprintf(stderr,"Illegal option\n");
	    showHelp(stderr);
	    exit(1);
	}
    }

    if ( verbose) fprintf(stderr,"Periodic file is %s\n", periodicFileName);

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

	// Data never comes faster than 5 seconds, we are looking at the second of a pair
	// of redundant transmissions
	//if ( oldestDatum && time(0)-oldestDatum < 5) continue;

	{
	    unsigned char *data = 0;
	    size_t dataLen = 0;
	    int bits = ook_decode_manchester( burst, 
					      200000, 715000,  // on short
					      715000, 1200000, // on long
					      200000, 650000,  // off short
					      650000, 1200000, //off long
					      &data, &dataLen,
					      verbose);
	    
	    if ( bits > 0) {
		unsigned nibbles = (bits+3)/4;
		unsigned char nibble[ nibbles];
		memset( nibble, 0, nibbles);
		for ( int i = 0; i < bits; i++) {
		    if (data[i]==0) {
			nibble[ i/4] += (1<<(i%4));  // least significant first
		    }
		}
		if ( verbose) {
		    fprintf(stderr, "Decoded manchester %d bits, %d nibbles ", bits, nibbles);
		    for ( int n = 0; n < nibbles; n++) {
			fprintf(stderr,"%x", nibble[n]);
		    }
		    fprintf(stderr,"\n");
		}

		if ( nibbles < 16) {
		    if ( verbose) fprintf(stderr,"too short to be valid data\n");
		    continue;
		}

		for ( int i = 0; i < 6; i++) {
		    if ( nibble[i] != 0x0f) {
			if ( verbose) fprintf(stderr,"sync bits were not all 0xf\n");
			continue;
		    }
		}

		if ( nibble[6] != 0xa) {
		    if ( verbose) fprintf(stderr,"preamble was not 0xa\n");
		    continue;
		}

		unsigned int sensorId = (nibble[7]<<12) + (nibble[8]<<8) + (nibble[9]<<4) + nibble[10];
		unsigned int channel = nibble[11];
		unsigned int rollingCode = (nibble[12]<<4)+nibble[13];
		unsigned int flags = nibble[14];
		if ( verbose) fprintf(stderr,"sensor=%04x channel=%d rollingcode=%d flags=0x%x\n", sensorId, channel, rollingCode, flags);

		// Sensor specific data begins at 15.
		switch( sensorId) {
		  case 0xf824:
		  case 0x1220:
		  case 0xf8b4:
		      {
			  if ( nibbles != 26) {
			      if ( verbose) fprintf(stderr,"Temperature/Humidity sensor data is wrong length, sensorid=%04x, lenght=%d needed 26\n", sensorId, nibbles);
			      break;
			  }
			  if ( okChecksum( nibble, 22)) {
			      int tempTenthsC = nibble[17]*100+nibble[16]*10+nibble[15];
			      if (nibble[18] != 0) tempTenthsC *= -1;
			      int relativeHum = nibble[20]*10 + nibble[19];
			      if ( verbose) fprintf(stderr,"Temp=%4.1fC Hum=%02d%% %d\n", tempTenthsC/10.0, relativeHum, nibbles);
			      addSample( &temperature, tempTenthsC/10.0);
			      addSample( &humidity, relativeHum);

			      recentTemp = tempTenthsC/10.0;
			      recentHum = relativeHum;
			  } else {
			      fprintf(stderr,"Bad checksum on sensor %04x\n", sensorId);
			  }

		      }
		      break;
		  case 0x2914:
		      {
			  if ( nibbles != 29) {
			      if ( verbose) fprintf(stderr,"Rain sensor data is wrong length, sensorid=%04x, lenght=%d needed 29\n", sensorId, nibbles);
			      break;
			  }
			  if ( okChecksum( nibble, 25)) {
			      const int inchesPerMeter = 1000.0/25.4;
			      int rainHundrethsPerHour = nibble[18]*1000+nibble[17]*100+nibble[16]*10+nibble[15]; // inch/100
			      int rainCount = nibble[24]*100000 + nibble[23]*10000 + nibble[22]*1000 +
				  nibble[21]*100 + nibble[20]*10 + nibble[19];                                    // inch/1000
			      if ( verbose) fprintf(stderr,"Rain=%4.1fin/hr Tot=%6d thousandths\n", rainHundrethsPerHour/100.0, rainCount);
			      if ( lastRainCounter < 0 || lastRainCounter > rainCount ) {  // if first or wrapped, just set for later
				  lastRainCounter = rainCount;
			      } else {
				  int r = (rainCount - lastRainCounter) * inchesPerMeter;
				  lastRainCounter = rainCount;
				  recentRain = r;
				  addSample(&rainfall, r);
			      }
			  } else {
			      fprintf(stderr,"Bad checksum on sensor %04x\n", sensorId);
			  }

		      }
		      break;
		  case 0x1984:
		  case 0x1994:
		      {
			  if ( nibbles != 28) {
			      if ( verbose) fprintf(stderr,"Wind sensor data is wrong length, sensorid=%04x, lenght=%d needed 28\n", sensorId, nibbles);
			      break;
			  }
			  if ( okChecksum( nibble, 24)) {
			      int direction = nibble[15];
			      // 16 and 17 are unknown
			      int currentSpeed = nibble[20]*100 + nibble[19]*10 + nibble[18];
			      int averageSpeed = nibble[23]*100 + nibble[22]*10 + nibble[21];

			      if ( verbose) fprintf(stderr,"Wind=%4.1fm/s avg=%4.1fm/s dir=%ds\n", currentSpeed/10.0, averageSpeed/10.0, direction);

			      addSample( &averageWindSpeed, averageSpeed/10.0);
			      addSample( &gustWindSpeed, currentSpeed/10.0);
			      addSample( &windDirection, direction);

			      recentWind = averageSpeed/10.0;
			      recentGust = currentSpeed/10.0;
			      recentDirection = direction;
			  } else {
			      fprintf(stderr,"Bad checksum on sensor %04x\n", sensorId);
			  }

		      }
		      break;
		    break;
		  default:
		    if (verbose) fprintf(stderr,"Unknown sensor: %04x\n", sensorId);
		}

		if ( oldestDatum && time(0)-oldestDatum > minutes*60) {
		    recordPeriodic( periodicFileName);
		}

		if ( verbose) dumpWeather();

		recordRecent( recentFileName, recentTemp, recentHum, recentWind, recentGust, recentRain, recentBattery, recentDirection);
	    } else {
		if ( verbose) fprintf(stderr,"ignored %d pulse burst\n", burst->pulses);
	    }

	    if (data) free(data);
	}

	fflush(stdin);
	free(burst);
    }

    close(sock);
    return 0;
}
