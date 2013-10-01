#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "ook.h"

int verbose=0;

static void showHelp( FILE *f)
{
    fprintf(f, 
	    "Usage: ookdump [-h] [-?] [-v] [-a mcastaddr] [-p mcastport] [-i mcastinterface]\n"
	    "  -h | -? | --help                      display usage and exit\n"
	    "  -v | --verbose                        verbose logging\n"
	    "  -a addr | --multicast-address addr    multicast address, default 236.0.0.1\n"
	    "  -p port | --multicast-port port       multicast port, default 3636\n"
	    "  -i addr | --multicast-interface addr  address of the multicast interface, default 127.0.0.1\n"
	    );
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
	    { "multicast-address", required_argument, 0, 'a'},
	    { "multicast-port", required_argument, 0, 'p' },
	    { "multicast-interface", required_argument, 0, 'i' },
	    { 0,0,0,0}
	};

	int c = getopt_long( argc, argv, "vh?f:a:p:i:", options, &optionIndex );
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
	  default:
	    fprintf(stderr,"Illegal option\n");
	    showHelp(stderr);
	    exit(1);
	}
    }


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
	
	printf("%014.6fs ### %3u pulses\n", burst->positionNanoseconds/1000000000.0, burst->pulses);
	printf("num high   low      freq\n");
	for ( int i = 0; i < burst->pulses; i++) {
	    printf( "%3u %4uuS %6uuS %8.3fkHz\n", i+1, burst->pulse[i].hiNanoseconds/1000, 
		    burst->pulse[i].lowNanoseconds/1000, burst->pulse[i].frequencyOffsetHz/1000.0);
	}

	fflush(stdin);
	free(burst);
    }

    close(sock);
    return 0;
}
