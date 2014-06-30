#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>
#include <math.h>

#include "ook.h"

//
// Data format comes from http://makin-things.com/articles/decoding-lacrosse-weather-sensor-rf-transmissions/
// And http://www.practicalarduino.com/projects/weather-station-receiver
//

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

static double currentTemperature = NAN;
static double currentHumidity = NAN;
static double currentWindSpeed = NAN;
static double currentGustSpeed = NAN;
static double currentWindDirection = NAN;

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
    dumpDatum( &rainfall, "rain", "mm");
    dumpDatum( &batteryLow, "batt", "??");
    dumpDatum( &windDirection, "dir", "NEWS");
}

static void reportRecent( const char *file)
{
    char name[256];
    strcpy(name,"/tmp/ws2300.XXXXXX");

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
    
    if ( !isnan(currentTemperature)) fprintf(f,"\t\"temperature\":%.1f,\n", currentTemperature);
    if ( !isnan(currentHumidity)) fprintf(f,"\t\"humidity\":%.1f,\n", currentHumidity);
    if ( !isnan(currentWindSpeed)) fprintf(f,"\t\"avgWindSpeed\":%.1f,\n", currentWindSpeed);
    if ( !isnan(currentGustSpeed)) fprintf(f,"\t\"gustSpeed\":%.1f,\n", currentGustSpeed);
    if ( !isnan(currentWindDirection)) fprintf(f,"\t\"windDirection\":%d,\n", (int)(currentWindDirection*45));
    fprintf(f,"\t\"timestamp\":%ld\n", time(0));
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
    strcpy(name,"/tmp/ws2300.XXXXXX");

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
	    "Usage: ws2300 [-h] [-?] [-v] [-a mcastaddr] [-p mcastport] [-i mcastinterface]\n"
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


int main( int argc, char **argv)
{
    const char *multicastAddress = "236.0.0.1";
    const char *multicastPort = "3636";
    const char *multicastInterface = "127.0.0.1";
    const char *recentFileName = "/tmp/current-weather.json";
    const char *periodicFileName = "/tmp/weather";
    int minutes = 5;

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

	{
	    unsigned char *data = 0;
	    size_t dataLen = 0;
	    int bits = ook_decode_pulse_width( burst, 
					       1300000,1500000, 250000,400000, 900000,UINT_MAX, 
					       &data, &dataLen,
					       verbose);
	    const int tx13_id = 0x06;
	    const int ws2300_id = 0x09;

	    if ( bits == 52 && (data[0] == tx13_id || data[0] == ws2300_id) ) {
		unsigned csum = 0;
		for ( int i = 0; i < 6; i++) {
		    unsigned highNibble = (data[i]>>4);
		    unsigned lowNibble = (data[i]&0x0f);
		    csum += highNibble + lowNibble;
		}
	        unsigned csumNibble = (csum & 0x0f);
		unsigned pcsumNibble = (data[6] & 0x0f);

		if ( csumNibble != pcsumNibble) {
		    fprintf(stderr,"Invalid checksum computed=0x%02x - packet says 0x%02x\n", csumNibble, pcsumNibble);
		    goto NotGood;
		}

		if ( verbose) {
		    for (int i = 0; i < dataLen; i++) fprintf(stderr,"%02x ", data[i]);
		    fprintf(stderr,"\n");
		}
		
		if ( oldestDatum && time(0)-oldestDatum > minutes*60) {
		    recordPeriodic( periodicFileName);
		}

		int packetId = ((data[1]>>4)&0x03);
		int stationId = ((data[1]&0x0f)<<4)+((data[2]&0xf0)>>4);

		if ( verbose) fprintf(stderr,"packetid=%d station=%d\n", packetId, stationId);

		switch( packetId) {
		  case 0:               // temp
		      {
			  double temp = (data[3]&0x0f)*10 + ((data[4]&0xf0)>>4) + (data[4]&0x0f)*0.1 - 30.0;  // TX13 is -40
			  addSample( &temperature, temp);
			  currentTemperature = temp;
		      }
		    break;
		  case 1:               // humidity
		      {
			  int hum = (data[3]&0x0f)*10 + ((data[4]&0xf0)>>4);
			  addSample( &humidity, hum);
			  currentHumidity = hum;
		      }
		    break;
		  case 2:               // rainfall
		      {
			  int rain = ((data[3]&0x0f)<<8) + data[4];
			  addSample( &rainfall, rain);
		      }
		    break;
		  case 3:               // wind
		      {
			  double wind = (((data[3]&0x1f)<<4) + ((data[4]&0xf0)>>4) ) / 10.0;
			  int windDir = (data[4] & 0x0f);
			  if ( data[1] & 0x80) {
			      if ( wind != 51.0) {
				  addSample( &gustWindSpeed, wind);
				  currentGustSpeed = wind;
			      }
			  } else {
			      if ( wind != 51.0) {
				  addSample( &averageWindSpeed, wind);
				  addSample( &windDirection, windDir);
				  currentWindSpeed = wind;
				  currentWindDirection = windDir;
			      }
			  }
		      }
		    break;
		}

		if ( verbose) dumpWeather();

		reportRecent(recentFileName);
	    } else {
		if ( verbose) fprintf(stderr,"ignored %d pulse burst\n", burst->pulses);
	    }

	  NotGood:
	    if (data) free(data);
	}

	fflush(stdin);
	free(burst);
    }

    close(sock);
    return 0;
}
