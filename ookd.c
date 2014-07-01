#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include "ook.h"
#include "rtl.h"

int verbose=0;
static uint32_t centerFrequency = 433910000;
static uint32_t sampleRate = 250000;

static uint64_t sampleCounter = 0;

static struct rtldev *rtlToStop = 0;   // used by signal handlers to stop cleanly.

static int multicastSocket = -1;
static struct sockaddr *multicastSockaddr = 0;
static size_t multicastSockaddrLen = 0;

static int minPacket = 16;

static void showHelp( FILE *f)
{
    fprintf(f, 
	    "Usage: ookd [-h] [-?] [-v] [-f frequency] [-a mcastaddr] [-p mcastport] [-i mcastinterface] [-m minpacket]\n"
	    "  -h | -? | --help                      display usage and exit\n"
	    "  -v | --verbose                        verbose logging\n"
	    "  -f nnnn | --frequency nnn             set center frequency, default 433910000\n"
	    "  -a addr | --multicast-address addr    multicast address, default 236.0.0.1\n"
	    "  -p port | --multicast-port port       multicast port, default 3636\n"
	    "  -i addr | --multicast-interface addr  address of the multicast interface, default 127.0.0.1\n"
	    "  -m nnnn | --min-packet nnnn           minimum number of pulses for a packet, default 10\n"
	    );
}

static uint64_t samplesToNs( uint64_t s)
{
    return s*(1000000000/sampleRate);  // that math could be better, but for 250000 is is exactly 4000, so ok.
}

static void recordPulse( unsigned n, unsigned rise, unsigned drop, unsigned end,
			 unsigned cw, unsigned ccw, unsigned crazy, unsigned terminal)
{
    static struct ook_burst *burst = 0;
    unsigned hiLen = drop-rise;
    unsigned lowLen = end-drop;
    
    // The frequency calculation could be a lot better. There is a lot of noise
    // in there which leads to misinterpretations of cw and ccw. There is a significant
    // variance in the pulse to pulse results of the same transmitter.
    float cycles = ((int)cw-(int)ccw)/4.0;
    if ( cycles > 0) cycles += crazy/2.0;  // figure we are going fast enough to sometimes skip
    if ( cycles < 0) cycles -= crazy/2.0;  // .. might ought to check that.
    float frequency = cycles/(hiLen/(float)sampleRate);

    if ( !burst) {
	burst = ook_allocate_burst(512*8);     // first pulse of new burst
	if ( !burst) {
	    fprintf(stderr,"Failed to allocate burst\n");
	    exit(-1);
	} else {
	    burst->positionNanoseconds = samplesToNs( sampleCounter + rise);
	}
    }

    if ( ook_add_pulse(burst, samplesToNs(hiLen), samplesToNs(lowLen), lrint(frequency))) {
	fprintf(stderr,"Failed to add pulse to burst! Too long?\n");
    }

    if ( terminal) {
	if ( burst) {
	    if ( burst->pulses > minPacket) {
		void *data=0;
		size_t len;
		if ( ook_encode( burst, &data, &len) != 0 || data == 0) {
		    fprintf(stderr, "Failed to encode a pulse burst.\n");
		} else {
		    int e = sendto( multicastSocket, data, len, 0, multicastSockaddr, multicastSockaddrLen);
		    if ( e < 0) {
			fprintf(stderr, "Failed to multicast pulse (%zu bytes): %s\n", len, strerror(errno));
		    }
		    if ( verbose) fprintf(stderr,"Multicast %u pulse, %zu bytes\n", burst->pulses, len);
		    
		    free(data);
		}
	    } else {
		if ( verbose) fprintf(stderr,"Skipped run burst of %d pulses\n", burst->pulses);
	    }

	    free(burst);
	    burst = 0;
	}
    }
}

static void debugPulses( const unsigned char *data, uint32_t len, uint8_t bins, const float alpha)
{
    enum motionType { NONE, CRAZY, CW, CCW };
    static const unsigned char motion[16] = {   // indexed by 4*oldquadrant+newquadrant
	NONE, CCW, CRAZY, CW,
	CW, NONE, CCW, CRAZY,
	CRAZY, CW, NONE, CCW,
	CCW, CRAZY, CW, NONE 
    };

    const int showAll = 0;
    static float lowPassPowerSquared = 0;

    static double totalPowerSquared = 0;
    static int powerSamples = 0;

    /*
    ** All of this static data is so we keep our state across
    ** invocations, it comes from being called in callbacks
    */
    static enum { IDLE, HIGH, LOW} state = IDLE;
    static int quadrant = 0;         // range 0-3

    static unsigned crazyMotion = 0;  // these three are signal rotation during pulse high period
    static unsigned cwMotion = 0;
    static unsigned ccwMotion = 0;

    const float riseThreshold = 0.25;
    const float dropThreshold = 0.1;
    const unsigned lowLengthLimit = 2000;

    static int riseSample = 0;
    static int dropSample = 0;
    static unsigned pulseNumber = 0;

    for ( int i = 0; i < len; i += 2) {
	float I = (data[i]-128)/128.0;
	float Q = (data[i+1]-128)/128.0;
	float powerSquared = I*I+Q*Q;

	totalPowerSquared += powerSquared;
	powerSamples++;

	if ( verbose && powerSamples >= 100000) {
	    fprintf(stderr,"average power is %5.2f\n", sqrt(totalPowerSquared/powerSamples));
	    powerSamples = 0;
	    totalPowerSquared = 0;
	}

	unsigned newQuadrant =0;

	if ( I >= 0) {
	    if ( Q >= 0) newQuadrant = 0;
	    else newQuadrant = 3;
	} else {
	    if ( Q >= 0) newQuadrant = 1;
	    else newQuadrant = 2;
	}
	if ( state==HIGH) {
	    if (showAll) fprintf(stderr,"%u->%u %5.2f %5.2f ", quadrant, newQuadrant, I, Q);
	    switch( motion[4*quadrant + newQuadrant]) {
	      case CRAZY:
		if ( showAll) fprintf(stderr," crazy\n");
		crazyMotion++;
		break;
	      case CW:
		if ( showAll) fprintf(stderr," cw\n");
		cwMotion++;
		break;
	      case CCW:
		if ( showAll) fprintf(stderr," ccw\n");
		ccwMotion++;
		break;
	      default:
		if ( showAll) fprintf(stderr,"\n");
		break;
	    }
	}
	quadrant = newQuadrant;

	lowPassPowerSquared = alpha*powerSquared + (1.0-alpha)*lowPassPowerSquared;

	if ( state==HIGH && lowPassPowerSquared < dropThreshold) {
	    dropSample = i/2;
	    state = LOW;
	} else if ( (state==IDLE || state==LOW) && lowPassPowerSquared > riseThreshold) {
	    if ( state==LOW) {  // if IDLE, the pulse was already pushed
		recordPulse( pulseNumber++, riseSample, dropSample, i/2, cwMotion, ccwMotion, crazyMotion, 0);
	    }
	    state = HIGH;
	    riseSample = i/2;
	    dropSample = 0;
	    cwMotion = 0;
	    ccwMotion = 0;
	    crazyMotion = 0;
	} else if ( state==LOW && i/2 - dropSample > lowLengthLimit ) {
	    state = IDLE;
	    recordPulse( pulseNumber, riseSample, dropSample, i/2, cwMotion, ccwMotion, crazyMotion, 1);
	    pulseNumber = 0;
	    // ok to leave counters and timers, they get set on transition to HIGH
	}
    }

    if ( state != IDLE) {         // shift them so they work on next invocation
	riseSample -= len/2;
	if ( state == LOW) dropSample -= len/2;
    }
}

#if 0
static void debugHistogram( const unsigned char *data, uint32_t len, uint8_t bins, const float alpha)
{
    unsigned bin[bins];
    unsigned sbin[bins];

    memset( bin, 0, sizeof(*bin)*bins);
    memset( sbin, 0, sizeof(*sbin)*bins);

    float s = 0;

    for ( int i = 0; i < len; i += 2) {
	float I = (data[i]-128)/128.0;
	float Q = (data[i+1]-128)/128.0;
	float powerSquared = I*I+Q*Q;

	s = alpha*powerSquared + (1.0-alpha)*s;
	
	float lowPassPowerSquared = s;

	unsigned b = powerSquared*(bins-1);
	if (b > bins-1) b = bins-1;
	bin[b]++;

	unsigned sb = lowPassPowerSquared*(bins-1);
	if (sb > bins-1) sb = bins-1;
	sbin[sb]++;
    }
    for ( int i = 0; i < bins; i++) {
	fprintf(stderr,"%5.3f %7u %7u\n", i/(float)bins, bin[i], sbin[i]);
    }
}
#endif

static void debugHandler(const unsigned char *data, uint32_t len, void *ctx, struct rtldev *rtl)
{
    //debugHistogram( data, len, 16, 0.2);
    debugPulses( data, len, 16, 0.2);
    sampleCounter += len/2;
}

static void exitNicely(int signum)
{
    if ( rtlToStop) {
	rtlStop( rtlToStop);
	rtlToStop = 0;
    } else {
	exit(0);      // we are stuck on something else
    }
}

static const char *humanName( struct sockaddr *addr, size_t len)
{
    static char buf[INET6_ADDRSTRLEN];
    int e = getnameinfo( addr, len, buf, sizeof(buf),0,0,NI_NUMERICHOST);
    if (e) return gai_strerror(e);
    return buf;
}

// contain OS specific nonsense here
static int setMulticastIF( int sock, const struct sockaddr *addr, size_t len)
{
#if __APPLE__
    switch ( addr->sa_family) {
      case AF_INET:
	return setsockopt( sock, IPPROTO_IP, IP_MULTICAST_IF, 
			   (char *)&(((struct sockaddr_in *)addr)->sin_addr), sizeof(struct in_addr));
      default:
	errno = EINVAL;
	return -1;
    }
#elif __linux__
    // Shouldn't this be a ip_mreqn or ip_mreq structure??
    return setsockopt( sock, IPPROTO_IP, IP_MULTICAST_IF, (char *)addr, len);
#else
#error Unsupported OS in setMulticastIF
#endif
}

// exit() on error
static void setupNetworking( const char *address, const char *port, const char *interface)
{
    // Parse our multicast address
    {
	struct addrinfo *ai = 0;
	struct addrinfo hints = { .ai_family = AF_UNSPEC,
				  .ai_socktype = SOCK_DGRAM,
	};

	int err = getaddrinfo( address, port, &hints, &ai);
	if (err){
	    fprintf(stderr,"Illegal multicast address (addr=%s port=%s):%s\n", address, port, gai_strerror(err));
	    exit(1);
	}
	// add a verbose print here
	
	multicastSockaddr = (struct sockaddr *)malloc( ai->ai_addrlen);
	memcpy( multicastSockaddr, ai->ai_addr, ai->ai_addrlen);
	multicastSockaddrLen = ai->ai_addrlen;

	freeaddrinfo(ai);
    }    

    // create our socket
    int sock = socket( multicastSockaddr->sa_family, SOCK_DGRAM, 0);
    if ( sock < 0) {
	fprintf(stderr,"Failed to create socket: %s\n", strerror(errno));
	exit(1);
    }

    // Set our multicast interface
    {
	struct addrinfo *ai = 0;
	struct addrinfo hints = { .ai_family = AF_UNSPEC,
				  .ai_socktype = SOCK_DGRAM,
	};

	int err = getaddrinfo( interface, "0", &hints, &ai);
	if (err){
	    fprintf(stderr,"Illegal interface address (addr=%s):%s\n", address, gai_strerror(err));
	    exit(1);
	}
	// add a verbose print here

	if ( setMulticastIF( sock, ai->ai_addr, ai->ai_addrlen) < 0) {
	    fprintf(stderr, "Failed to set multicast interface to %s (%s): %s\n", 
		    interface, humanName(ai->ai_addr, ai->ai_addrlen), strerror(errno));
	    exit(1);
	}

	// enable loopback so clients can be on this host
	uint8_t loop=1;
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	freeaddrinfo(ai);
    }

    // Leave TTL defaulted to 1 for now, stay on subnet.


    multicastSocket = sock;
}

int main( int argc, char **argv)
{
    const char *multicastAddress = "236.0.0.1";
    const char *multicastPort = "3636";
    const char *multicastInterface = "127.0.0.1";

    // Handle options
    for(;;) {
	int optionIndex = 0;
	static struct option options[] = {
	    { "verbose", no_argument, 0, 'v' },
	    { "help",    no_argument, 0, 'h' },
	    { "frequency", required_argument, 0, 'f' },
	    { "multicast-address", required_argument, 0, 'a'},
	    { "multicast-port", required_argument, 0, 'p' },
	    { "multicast-interface", required_argument, 0, 'i' },
	    { "min-packet", required_argument, 0, 'm' },
	    { 0,0,0,0}
	};

	int c = getopt_long( argc, argv, "vh?f:a:p:i:m:", options, &optionIndex );
	if ( c == -1) break;

	switch(c) {
	  case 'h':
	  case '?':
	    showHelp(stdout);
	    return 0;
	  case 'v':
	    verbose = 1;
	    break;
	  case 'f':
	    // set frequency to optarg
	      {
		  unsigned f = atoi( optarg);
		  if (f==0) {
		      fprintf(stderr,"Bad frequency: %s\n", optarg);
		      exit(1);
		  }
		  centerFrequency = f;
	      }
	    break;
	  case 'm':
	    minPacket = atoi(optarg);
	    break;
	  case 'a':
	    multicastAddress = optarg;
	    break;
	  case 'i':
	    multicastInterface = optarg;
	    break;
	  case 'p':
	    multicastPort = optarg;
	    break;
	  default:
	    fprintf(stderr,"Illegal option\n");
	    showHelp(stderr);
	    exit(1);
	}
    }

    setupNetworking(multicastAddress, multicastPort, multicastInterface);

    signal(SIGINT, exitNicely);

    {
	struct rtldev *rtl = rtlOpen(NULL,0);
	if ( !rtl) {
	    fprintf(stderr,"Failed to open RTL SDR device\n");
	    exit(1);
	}

	if ( rtlSetup( rtl, centerFrequency, sampleRate) < 0) {
	    fprintf(stderr,"Failed to setup RTL SDR for %uHz %usamp/sec\n", centerFrequency, sampleRate);
	}

	// something must call rtlStop(rtl) to kill this, to this end we stash in a global, ick
	rtlToStop = rtl;
	if ( rtlRun( rtl, debugHandler, 0)) {
	    fprintf(stderr, "Failed to run debugHandler\n");
	}
	rtlToStop = 0;

	rtlClose(rtl);

	//
	// the rest of this is just in case someone is running a leak detector on us.
	//
	if ( multicastSocket != -1) {
	    close(multicastSocket);
	    multicastSocket = -1;
	}
	if ( multicastSockaddr) {
	    free(multicastSockaddr);
	    multicastSockaddr = 0;
	    multicastSockaddrLen = 0;
	}
    }

    return 0;
}
