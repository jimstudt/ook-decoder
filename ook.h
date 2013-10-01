#ifndef OOK_IS_IN
#define OOK_IS_IN

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

struct ook_pulse {
    uint32_t hiNanoseconds;
    uint32_t lowNanoseconds;
    int32_t frequencyOffsetHz;
};

struct ook_burst {
    uint64_t positionNanoseconds;  // relative to when the daemon started
    uint32_t pulses;
    uint32_t allocatedPulses;      // how many pulses can be stored in here
    struct ook_pulse pulse[];
};

// Free with free(), return NULL on error
struct ook_burst *ook_allocate_burst( uint32_t maximumPulses);

// -1 if tried to overflow or bad burst, 0 if ok
int ook_add_pulse( struct ook_burst *burst, uint32_t hiNs, uint32_t lowNs, int32_t freqOffsetHz);

// Serialize an ook_pulse into a sequence of bytes. 
// return 0 if ok
// dataReturn should be free()d if it is set.
int ook_encode( struct ook_burst *burst, void **dataReturn, size_t *sizeReturn);

// Get a socket bound for listening for pulse bursts, -1 on error
// This handles the rather tedious UDP multicast jiggery
int ook_open( const char *address, const char *port, const char *interface);

// -1 socket error, 0 bad packet (from/fromLen valid), >0 good burst (burstReturn/from/fromLen valid)
// Will block awaiting data. You should use select() if that isn't for you.
// If burstReturn is set, it must be free()ed.
int ook_decode_from_socket( int sock, struct ook_burst **burstReturn, struct sockaddr *from, socklen_t *fromLen, int verbose);

// This is for decoding pulse width encoding. The bits are determined by the length of the high part
// of the pulse, the lows are important for timing, but not data bits.
// -1 illegal pulse in there, otherwise number bits!! read that again, bits, in data. datLen is in bytes.
// The last byte may not be full, live bits will be in the least significant bits
// pulse time limits are in nanoseconds
// if data is non-NULL it must be free()ed.
// If verbose is set, then it will print diagnostics to stderr.
int ook_decode_pulse_width( struct ook_burst *burst, 
			    uint32_t minZeroHi, uint32_t maxZeroHi, 
			    uint32_t minOneHi, uint32_t maxOneHi, 
			    uint32_t minLow, uint32_t maxLow, 
			    unsigned char **dataReturn, size_t *dataLenReturn,
			    int verbose);

#endif
