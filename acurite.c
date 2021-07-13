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

int verbose=0;

struct report {
    uint32_t valid:1;
    uint32_t channel:2;
    uint32_t id:14;
    uint32_t temperature:12;
    uint32_t humidity:8;
    uint32_t batteryLow:1;
};

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
    return high_ns >= 200000 && high_ns <= 300000 && low_ns >= 1000000;
}

static struct report decode_acurite( const struct ook_burst *burst) {
    enum { IDLE=0, STARTS, CONTENT } state = IDLE;
    unsigned idleGarbage = 0;
    unsigned falseStarts = 0;
    
    uint64_t data = 0;
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
	    data = 0;
	    // FALLTHROUGH!!!!!!!
	  case CONTENT:
	    if ( isOne(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds) ) {
		data = (data<<1) + 1;
		bits++;
	    } else if ( isZero(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) {
		data = data<<1;
		bits++;
	    } else if ( isStop(burst->pulse[p].hiNanoseconds, burst->pulse[p].lowNanoseconds)) {
		if (verbose) fprintf(stderr, "At stop we have %d bits, %llx\n", bits, data);
		
		state = IDLE;

		const uint8_t chanMap[] = { 3, 0, 2, 1};
		
		uint8_t channel = chanMap[ (data>>54) & 0x03];
		uint16_t id = (data>>40) & 0x3fff;
		uint8_t battery = (data>>38) & 1;
		uint8_t message = (data>>32) & 0x2f;
		uint8_t mParity = (data>>39) & 1;
		
		if ( mParity != ( (__builtin_popcount( message) + battery) & 1)) {
		    if ( verbose) fprintf(stderr, "parity error in message code\n");
		    state = IDLE;
		    continue;
		}

		if ( message != 4 || bits != 56) {
		    if ( verbose) fprintf(stderr, "Not a 592TXR message\n");
		    state = IDLE;
		    continue;
		}

		uint8_t humidity = (data>>24) & 0x7f;
		uint8_t hParity = (data>>31) & 1;
		if ( hParity != (__builtin_popcount( humidity) & 1)) {
		    if ( verbose) fprintf(stderr, "parity error in humidity\n");
		    state = IDLE;
		    continue;
		}

		uint8_t tempHigh = (data>>16) & 0x7f;
		uint8_t tempLow = (data>>8) & 0x7f;
		uint16_t temperature = (tempHigh<<7) | tempLow;

		uint8_t tHighParity = (data>>23) & 1;
		uint8_t tLowParity = (data>>15) & 1;

		if ( tHighParity != (__builtin_popcount( tempHigh) & 1) ||
		     tLowParity != (__builtin_popcount( tempLow) & 1)) {
		    if ( verbose) fprintf(stderr, "parity error in temperature\n");
		    state = IDLE;
		    continue;
		}
		if ( verbose) fprintf(stderr, "  chan=%d id=%d bat=%d msg=%d temperature=%d hum=%d\n", channel, id, battery, message, temperature,  humidity);
		struct report good = { .valid = 1,
				       .channel = channel,
				       .id = id,
				       .batteryLow = !battery,
				       .temperature = temperature,
				       .humidity = humidity};
		return good;
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
    fprintf(f,"\t\"temperature\":%.1f,\n", (report->temperature / 10.0) - 100.0 );
    fprintf(f,"\t\"humidity\":%d,\n", report->humidity);
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
