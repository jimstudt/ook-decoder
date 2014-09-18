#include "datum.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void resetDatum(struct datum *d)
{
    memset(d,0,sizeof(*d));
}
void resetCDatum(struct cdatum *d)
{
    memset(d,0,sizeof(*d));
}


void addSample( struct datum *d, double v)
{
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

void addCSample( struct cdatum *d, double complex v)
{
    if ( d->n==0) {
	d->minimum = v;
	d->maximum = v;
    } else {
	double mag = cabs(v);
	if ( mag > cabs(d->maximum)) d->maximum = v;
	if ( mag < cabs(d->minimum)) d->minimum = v;
    }
    d->n++;
    d->sum += v;
    d->sumOfSquares += v*v;
}

void addCSampleMA( struct cdatum *d, double magnitude, double angle)
{
    addCSample( d, magnitude*cos(angle) + I*magnitude*sin(angle) );
}



void dumpDatum( struct datum *d, const char *name, const char *units)
{
    if ( d->n == 0) {
	fprintf(stderr,"%s no data\n", name);
    } else {
	fprintf(stderr, "%s %u samples, %5.1f %s   min %5.1f  max %5.1f\n",
		name, d->n, d->sum/d->n, units, d->minimum, d->maximum);
    }
}

void dumpCDatum( struct cdatum *d, const char *name, const char *units)
{
    if ( d->n == 0) {
	fprintf(stderr,"%s no data\n", name);
    } else {
	double complex avg = creal(d->sum)/d->n + I*(cimag(d->sum)/d->n);

	fprintf(stderr, "%s %u samples, (%5.1f+%5.1fi)(%5.1f @ %5.1fdeg) %s   min (%5.1f+%5.1fi)  max (%5.1f+%5.1fi)\n",
		name, d->n, 
		creal(avg),cimag(avg), 
		cabs(avg), carg(avg),
		//cabs(avg), carg(avg)*360.0/M_2_PI,
		units, 
		creal(d->minimum), cimag(d->minimum), 
		creal(d->maximum), cimag(d->maximum));
    }
}


