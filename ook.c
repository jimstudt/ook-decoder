#include "ook.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

struct ook_burst *ook_allocate_burst( uint32_t maximumPulses)
{
    struct ook_burst *r = 0;
    size_t need = sizeof(*r)+maximumPulses*sizeof(r->pulse[0]);

    r = malloc(need);
    if ( r) {
	r->positionNanoseconds = 0;
	r->pulses = 0;
	r->allocatedPulses = maximumPulses;
    }
    return r;
}


int ook_add_pulse( struct ook_burst *burst, uint32_t hiNs, uint32_t lowNs, int32_t freqOffsetHz)
{
    if ( burst->pulses < burst->allocatedPulses) {
	burst->pulse[burst->pulses].hiNanoseconds = hiNs;
	burst->pulse[burst->pulses].lowNanoseconds = lowNs;
	burst->pulse[burst->pulses].frequencyOffsetHz = freqOffsetHz;
	burst->pulses++;
	return 0;
    }
    return -1;
}

int ook_encode( struct ook_burst *burst, void **dataReturn, size_t *sizeReturn)
{
    size_t maxSize = sizeof(*burst) + sizeof(burst->pulse[0])*burst->pulses + 64 /* some packet overhead */;
    void *data = malloc( maxSize);
    if ( data == 0) return -1;

    void *thumb = data;
    size_t left = maxSize;

#define OPUT_U32(V) { if ( left < 4) goto Overflow; memcpy( thumb, &(V), 4); thumb+=4; left-=4; }
#define OPUT_I32(V) { if ( left < 4) goto Overflow; memcpy( thumb, &(V), 4); thumb+=4; left-=4; }
#define OPUT_U64(V) { if ( left < 8) goto Overflow; memcpy( thumb, &(V), 8); thumb+=8; left-=8; }

    uint32_t vers = 0x36360001; 
    OPUT_U32( vers);  // version signature
    OPUT_U64( burst->positionNanoseconds);
    OPUT_U32( burst->pulses);
    for ( int i = 0; i < burst->pulses; i++) {
	OPUT_U32( burst->pulse[i].hiNanoseconds);
	OPUT_U32( burst->pulse[i].lowNanoseconds);
	OPUT_I32( burst->pulse[i].frequencyOffsetHz);
    }

    *dataReturn = data;
    *sizeReturn = thumb-data;
    return 0;

  Overflow:
    return -1;
}

int ook_open( const char *address, const char *port, const char *interface)
{
    int sock = -1;
    struct addrinfo *multicast_ai = 0;
    struct addrinfo *interface_ai = 0;
    struct addrinfo hints = { .ai_family = AF_UNSPEC,
			      .ai_socktype = SOCK_DGRAM,
    };

    int err = getaddrinfo( address, port, &hints, &multicast_ai);
    if (err){
	fprintf(stderr,"Illegal multicast address (addr=%s port=%s):%s\n", address, port, gai_strerror(err));
	goto Fail;
    }

    err = getaddrinfo( interface, port, &hints, &interface_ai);
    if (err){
	fprintf(stderr,"Illegal interface address (addr=%s port=%s):%s\n", interface, port, gai_strerror(err));
	goto Fail;
    }

    // add a verbose print here
	
    sock = socket( interface_ai->ai_family, SOCK_DGRAM, 0);
    if ( sock < 0) {
	fprintf(stderr,"Failed to create socket: %s\n", strerror(errno));
	goto Fail;
    }

    int reuse=1;
    if ( setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
	fprintf(stderr, "Failed to reuse socket: %s\n", strerror(errno));
	goto Fail;
    }

    switch( multicast_ai->ai_family) {
      case AF_INET:
	  {
	      struct sockaddr_in anySock = { .sin_family = AF_INET,
					     .sin_port = ((struct sockaddr_in *)multicast_ai->ai_addr)->sin_port,
					     .sin_addr.s_addr = INADDR_ANY };
	      if ( bind( sock, (struct sockaddr *)&anySock, sizeof(anySock)) < 0) {
		  fprintf(stderr,"Failed to bind to multicast interface: %s\n", strerror(errno));
		  goto Fail;
	      }

	      struct ip_mreq group;
	      group.imr_multiaddr.s_addr = ((struct sockaddr_in *)multicast_ai->ai_addr)->sin_addr.s_addr;
	      group.imr_interface.s_addr = ((struct sockaddr_in *)interface_ai->ai_addr)->sin_addr.s_addr;

	      int r = setsockopt( sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&group, sizeof(group));
	      if ( r < 0) {
		  fprintf(stderr,"Failed to join multicast group: %s\n", strerror(errno));
		  goto Fail;
	      }
	  }
	  break;
      case AF_INET6:
	  {
	      struct sockaddr_in6 anySock = { .sin6_family = AF_INET6,
					      .sin6_port = ((struct sockaddr_in6 *)multicast_ai->ai_addr)->sin6_port,
					      .sin6_addr = in6addr_any };
	      if ( bind( sock, (struct sockaddr *)&anySock, sizeof(anySock)) < 0) {
		  fprintf(stderr,"Failed to bind to multicast interface: %s\n", strerror(errno));
		  goto Fail;
	      }

	      struct ipv6_mreq group;
	      memcpy( &group.ipv6mr_multiaddr, &((struct sockaddr_in6 *)multicast_ai->ai_addr)->sin6_addr, sizeof(struct in6_addr));
	      group.ipv6mr_interface = 0;
		  
	      int r = setsockopt( sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, (const void *)&group, sizeof(group));
	      if ( r < 0) {
		  fprintf(stderr,"Failed to join multicast group: %s\n", strerror(errno));
		  goto Fail;
	      }
	  }
	  break;
      default:
	fprintf(stderr, "Unsupported family for multicast groups: %d\n", multicast_ai->ai_family);
	goto Fail;
	break;
    }

    freeaddrinfo( multicast_ai);
    freeaddrinfo( interface_ai);
    
    return sock;

  Fail:
    if ( multicast_ai) freeaddrinfo( multicast_ai);
    if ( interface_ai) freeaddrinfo( interface_ai);
    if ( sock >= 0) close(sock);

    return -1;
}


int ook_decode_from_socket( int sock, struct ook_burst **burstReturn, struct sockaddr *from, socklen_t *fromLen, int verbose)
{
    struct ook_burst *burst = 0;
    unsigned char buf[65536];
    int e=0;

    do {
	e = recvfrom( sock, buf, sizeof(buf), 0, from, fromLen);
	if ( e == -1 && (errno == EAGAIN || errno == EINTR)) continue;
	if ( e == -1) return -1;
    } while(0);

    if ( verbose) fprintf(stderr,"Received %u bytes\n", e);

    uint32_t left = e;
    unsigned char *thumb = buf;

#define OGET_U32() ({ uint32_t v; if ( left<4) goto Fail; memcpy(&v,thumb,4); thumb+=4; left -= 4; v; })
#define OGET_I32() ({ int32_t v; if ( left<4) goto Fail; memcpy(&v,thumb,4); thumb+=4; left -= 4; v; })
#define OGET_U64() ({ uint64_t v; if ( left<8) goto Fail; memcpy(&v,thumb,8); thumb+=8; left -= 8; v; })

    uint32_t vers = OGET_U32();
    if ( vers != 0x36360001) goto Fail;

    uint64_t pos = OGET_U64();
    uint32_t pulses = OGET_U32();

    burst = ook_allocate_burst( pulses);
    if ( !burst) goto Fail;

    burst->positionNanoseconds = pos;
    for ( int i = 0; i < pulses; i++) {
	uint32_t hi = OGET_U32();
	uint32_t low = OGET_U32();
	int32_t freq = OGET_I32();

	if ( ook_add_pulse( burst, hi, low, freq) < 0) goto Fail;
    }

    if ( left > 0) goto Fail;

    *burstReturn = burst;

    return 1;
    
  Fail:
    *burstReturn = 0;
    if (burst) free(burst);

    return 0;
}

int ook_decode_pulse_width( struct ook_burst *burst, 
			    uint32_t minZeroHi, uint32_t maxZeroHi, 
			    uint32_t minOneHi, uint32_t maxOneHi, 
			    uint32_t minLow, uint32_t maxLow, 
			    unsigned char **dataReturn, size_t *dataLenReturn,
			    int verbose)
{
    size_t dataLen = (burst->pulses + 7)/8;
    unsigned char *data = (unsigned char *)malloc( dataLen);
    if ( data == 0) goto Fail;

    unsigned char accum = 0;
    unsigned char *thumb = data;
    unsigned char bitsInAccum = 0;
    unsigned bits = 0;
    for ( int i = 0; i < burst->pulses; i++) {
	uint32_t hi = burst->pulse[i].hiNanoseconds;
	uint32_t low = burst->pulse[i].lowNanoseconds;

	if ( low < minLow || low > maxLow) {
	    if ( verbose) fprintf(stderr,"low of %u was %u, not between %u and %u\n", i, low, minLow, maxLow);
	    goto Fail;
	}

	if ( hi >= minZeroHi && hi <= maxZeroHi) {
	    accum = (accum<<1);
	    bitsInAccum++;
	    bits++;
	} else if ( hi >= minOneHi && hi <= maxOneHi) {
	    accum = ((accum<<1)|1);
	    bitsInAccum++;
	    bits++;
	} else {
	    if ( verbose) fprintf(stderr,"high of %u was %u, not between %u and %u or %u and %u\n", 
				  i, hi, minZeroHi, maxZeroHi, minOneHi, maxOneHi);
	    goto Fail;
	}

	if ( bitsInAccum == 8) {
	    *thumb++ = accum;
	    bitsInAccum = 0;
	}
    }
    if ( bitsInAccum != 0) {
	*thumb++ = accum;
	bitsInAccum = 0;
    }

    *dataReturn = data;
    *dataLenReturn = dataLen;
    return bits;

  Fail:
    if ( data) free(data);
    return -1;
}

