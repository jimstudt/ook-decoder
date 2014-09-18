#ifndef SAMPLES_IS_IN
#define SAMPLES_IS_IN

#include <complex.h>

// Datum keeps enough information to sum them up and still compute the standard deviation
struct datum {
    unsigned n;
    double sum;
    double sumOfSquares;
    double maximum;
    double minimum;
};

// Datum keeps enough information to sum them up and still compute the standard deviation
struct cdatum {
    unsigned n;
    double complex sum;
    double complex sumOfSquares;
    double complex maximum;
    double complex minimum;
};

void resetDatum(struct datum *d);
void resetCDatum(struct cdatum *d);

void addSample( struct datum *d, double v);
void addCSample( struct cdatum *d, double complex v);
void addCSampleMA( struct cdatum *d, double magnitude, double angle);

void dumpDatum( struct datum *d, const char *name, const char *units);
void dumpCDatum( struct cdatum *d, const char *name, const char *units);



#endif
