#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rtl.h"

const int magic = 0x471005D4;

static int rtlOk( const struct rtldev *r)
{
    if ( !r || r->magic != magic) return 0;
    return 1;
}

struct rtldev *rtlOpen( const char *serial, int32_t ind)
{
    if ( ind < 0) {
	if ( serial) {
	    ind = rtlsdr_get_index_by_serial( serial);
	    if ( ind < 0) return 0;
	} else {
	    ind = 0;
	}
    }

    rtlsdr_dev_t *dev = 0;
    int e = rtlsdr_open( &dev, (uint32_t)ind);

    if ( e == 0 && dev != 0) {
	struct rtldev *r = calloc( sizeof(*r), 1);
	r->magic = magic;
	r->dev = dev;
	return r;
    } else {
	return 0;
    }
}

void rtlClose( struct rtldev *r)
{
    if (!r) return;
    if (!rtlOk(r)) {
	fprintf(stderr,"RTL SDR passed to rtlClose is corrupted\n");
	return;
    }

    rtlsdr_cancel_async( r->dev);
    // don't check error, looks like it gives -2 if you weren't streaming.

    int e  = rtlsdr_close( r->dev);
    if ( e) {
	fprintf(stderr,"Failed to close RTL SDR device\n");
    }

    memset(r, 0, sizeof(*r));  // make invalid
    free(r);
}

int rtlSetup( struct rtldev *r, uint32_t frequency, uint32_t sampleRate)
{
    if ( !rtlOk(r)) {
	return -1;
    }

    int e = rtlsdr_set_center_freq( r->dev, frequency);
    if (e) {
	fprintf(stderr,"Failed to set center frequency to %uHz\n", frequency);
	return -1;
    }

    int s = rtlsdr_set_sample_rate( r->dev, sampleRate);
    if (s) {
	fprintf(stderr,"Failed to set sample rate to %usamples/sec\n", sampleRate);
	return -1;
    }

    int a = rtlsdr_set_tuner_gain_mode( r->dev, 0);
    if ( a) {
	fprintf(stderr,"Failed to set tuner automatic gain mode\n");
	return -1;
    }

    return 0;
}

struct rtlHandlerState {
    struct rtldev *rtl;
    sdr_handler handler;
    void *ctx;
};

static void rtlHandler(unsigned char *buf, uint32_t len, void *ctx)
{
    struct rtlHandlerState *state = (struct rtlHandlerState *)ctx;

    state->handler( buf, len, state->ctx, state->rtl);
}

int rtlRun( struct rtldev *r, sdr_handler handler, void *ctx)
{
    if ( !rtlOk(r)) {
	return -1;
    }

    int re = rtlsdr_reset_buffer( r->dev);  // this is important
    if ( re) return 0;

    struct rtlHandlerState state = { r, handler, ctx };

    int e = rtlsdr_read_async( r->dev, rtlHandler, (void *)&state, 0,0);

    if ( e != 0) return 0;

    return 0;
}

int rtlStop( struct rtldev *r)
{
    if ( !rtlOk(r)) {
	return -1;
    }
    return rtlsdr_cancel_async(r->dev);
}

