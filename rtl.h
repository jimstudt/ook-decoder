#ifndef RTL_IS_IN
#define RTL_IS_IN

/*
** This is vaguely similar to the rtl-sdr API, but is slightly easier to use for simple use cases,
** and safer.
**
** It will print errors to stderr for programmer misuse or misconfiguration.
**
*/

#include <rtl-sdr.h>

struct rtldev {
    int magic;                     // set to 0x471005D4 if this is live, 
    rtlsdr_dev_t *dev;
};

typedef void (*sdr_handler)(const unsigned char *data, uint32_t len, void *ctx, struct rtldev *rtl);


/*
** Open by name or index.
**
**   If index is not -1, then it will be used to select the device
**   If serial is not NULL then it will be used to select the device
**   Otherwise the first device is selected
**
**   NULL is returned for failure
*/
struct rtldev *rtlOpen( const char *serial, int32_t index);

/*
** Close and free an open device, it is ok to pass in NULL
*/
void rtlClose( struct rtldev *rtl);

/*
** Tune the center frequency and sample rate.
**   
**   Returns <0 on failure.
*/
int rtlSetup( struct rtldev *r, uint32_t centerFrequency, uint32_t sampleRate);

/*
** Begin processing the signal. It will repeatedly 
** invoke your handler and pass it buffers.
**
** This will continue until an rtlStop() is used. This might happen
** in your handler, or perhaps in a signal() handler.
*/
int rtlRun( struct rtldev *rtl, sdr_handler handler, void *ctx);

/*
** Stop processing the signal, if running
** Safe to call from signal() handlers.
*/
int rtlStop( struct rtldev *rtl);


#endif
