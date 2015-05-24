#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ook.h"

/* Nexa protocol specification was used from http://tech.jolowe.se/home-automation-rf-protocols/ */

#define SYNC_BIT_LEN 11
#define PAUSE_BIT_LEN 41
#define PHYSICAL_1_BIT_LEN 2
#define PHYSICAL_0_BIT_LEN 6
#define TRANSMITTER_CODE_LEN 26

#define PULSE_LENGTH_NANOSEC 250000

#define STATSD_HOST "127.0.0.1"
#define STATSD_PORT 8125

int verbose = 0;

struct nexa_p
{
    /* transmitter unique code, and it is this code that the reciever "learns" to recognize */
    uint32_t transmitter_code:TRANSMITTER_CODE_LEN;
    
    /* 1 - on, 0 - off */
    uint8_t group_code:1;
    
    /* 1 - on, 0 - off */
    uint8_t on_off:1;
    
    /* Channel bits. Proove/Anslut = 00, Nexa = 11 */
    uint8_t channel_bits:2;
    
    /* Unit bits. Device to be turned on or off.
     Proove/Anslut Unit #1 = 00, #2 = 01, #3 = 10.
     Nexa Unit #1 = 11, #2 = 10, #3 = 01 */
    uint8_t unit_bits:2;
};

/*
 Returns 1 if the data pointer points to a "sync" bit, 0 otherwise
 */
int is_sync_bit(unsigned char** data)
{
    if((*data)[0] == 1) {
        for(int i = 1; i < SYNC_BIT_LEN; ++i) {
            if((*data)[i] != 0) {
                return 0;
            }
        }
    }
    
    *data += SYNC_BIT_LEN;
    return 1;
}

/*
 Returns 1 if the data pointer points to a "pause" bit, 0 otherwise
 */
int is_pause_bit(unsigned char** data)
{
    if((*data)[0] == 1) {
        for(int i = 1; i < PAUSE_BIT_LEN; ++i) {
            if((*data)[i] != 0) {
                return 0;
            }
        }
    }
    
    *data += PAUSE_BIT_LEN;
    return 1;
}

/*
 Decodes physical bit from the pulses sequence
 Returns bit value (1 or 0)
 */
int decode_physical_bit(unsigned char** data)
{
    if((*data)[0] == 1) {
        for(int i = 1; i < PHYSICAL_0_BIT_LEN; ++i) {
            if((*data)[i] != 0) {
                *data += PHYSICAL_1_BIT_LEN;
                return 1;
            }
        }
    }
    
    *data += PHYSICAL_0_BIT_LEN;
    return 0;
}

/*
 Decodes logical bit from the pair of 2 physical bits
 Returns bit value on success (1 or 0), -1 on error
 */
int decode_logical_bit(unsigned char** data)
{
    int first_bit = decode_physical_bit(data);
    int second_bit = decode_physical_bit(data);
    
    if(first_bit && !second_bit) {
        return 1;
    } else if(!first_bit && second_bit) {
        return 0;
    } else if(verbose) {
        fprintf(stderr, "incorrect physical bit sequence %d %d\n", first_bit, second_bit);
    }
    
    return -1;
}

/*
 Decodes set of physical bits to the Nexa control packet structure
 Returns a filled struct on success, NULL on error
 */
struct nexa_p* decode_nexa_p(unsigned char* data)
{
    if(is_sync_bit(&data)) {
        struct nexa_p* packet = (struct nexa_p*)malloc(sizeof(struct nexa_p));
        memset(packet, 0, sizeof(struct nexa_p));
        
        for(int i = 0; i < TRANSMITTER_CODE_LEN; ++i) {
            uint8_t decoded_bit = decode_logical_bit(&data);

            if(decoded_bit == 1) {
                packet->transmitter_code |= (1 << i);
            }
        }
        
        if(decode_logical_bit(&data) == 0) {
            packet->group_code = 1; // inversed by protocol
        }
        
        if(decode_logical_bit(&data) == 0) {
            packet->on_off = 1; // inversed by protocol
        }
        
        if(decode_logical_bit(&data) == 1) {
            packet->channel_bits |= 1;
        }
        if(decode_logical_bit(&data) == 1) {
            packet->channel_bits |= (1 << 1);
        }
        
        if(decode_logical_bit(&data) == 1) {
            packet->unit_bits |= 1;
        }
        if(decode_logical_bit(&data) == 1) {
            packet->unit_bits |= (1 << 1);
        }
        
        if(is_pause_bit(&data)) {
            return packet;
        } else if(verbose) {
            fprintf(stderr, "no pause bit\n");
            free(packet);
        }
    } else if(verbose) {
        fprintf(stderr, "no sync bit\n");
    }
    
    return NULL;
}

/*
 Sends "1" to the gauge metric of StatsD server
 Returns 0 on success, -1 on error
 */
int send_statsd_gauge(const char* metricName)
{
    struct sockaddr_in si_other;
    int s, slen = sizeof(si_other);
    char buf[128];
    
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        return -1;
    }
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(STATSD_PORT);
    if (inet_aton(STATSD_HOST, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed for address '%s'\n", STATSD_HOST);
        return -1;
    }
    
    snprintf(buf, sizeof(buf), "%s:1|g", metricName);
    if (sendto(s, buf, strlen(buf), 0, (struct sockaddr*)&si_other, slen) == -1) {
        return -1;
    }
    
    close(s);
    return 0;
}

static void showHelp( FILE *f)
{
    fprintf(f,
            "Usage: nexa [-h] [-?] [-v] [-a mcastaddr] [-p mcastport] [-i mcastinterface] [-f transmittercode] [-m metricname]\n"
            "  -h | -? | --help                      display usage and exit\n"
            "  -v | --verbose                        verbose logging\n"
            "  -a addr | --multicast-address addr    multicast address, default 236.0.0.1\n"
            "  -p port | --multicast-port port       multicast port, default 3636\n"
            "  -i addr | --multicast-interface addr  address of the multicast interface, default 127.0.0.1\n"
            "  -f code | --filter-transmitter-code   transmitter code to filter output with, disabled by default\n"
            "  -m name | --metric-name               name of the gauge metric to send to StatsD server, disabled by default\n"
            );
}

int main( int argc, char **argv)
{
    const char *multicastAddress = "236.0.0.1";
    const char *multicastPort = "3636";
    const char *multicastInterface = "127.0.0.1";
    int32_t filterTransmitterCode = -1;
    const char *metricName = NULL;
    
    // Handle options
    for(;;) {
        int optionIndex = 0;
        static struct option options[] = {
            { "verbose", no_argument, 0, 'v' },
            { "help",    no_argument, 0, 'h' },
            { "multicast-address", required_argument, 0, 'a'},
            { "multicast-port", required_argument, 0, 'p' },
            { "multicast-interface", required_argument, 0, 'i' },
            { "filter-transmitter-code", required_argument, 0, 'f' },
            { "metric-name", required_argument, 0, 'm' },
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
            case 'f':
                filterTransmitterCode = atoi(optarg);
                break;
            case 'm':
                metricName = optarg;
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
    
    unsigned char *data = (unsigned char *)malloc(4096); // predefined upper limit of physical bits
    
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
        
        size_t bits = 0;
        memset(data, 0, sizeof(data));
        
        for ( int i = 0; i < burst->pulses; i++) {
            data[bits++] = 1;
            
            uint8_t zeros = burst->pulse[i].lowNanoseconds / PULSE_LENGTH_NANOSEC;
            while(zeros--) {
                data[bits++] = 0;
            }
        }
        
        struct nexa_p* packet = decode_nexa_p(data);
        if(packet) {
            if(filterTransmitterCode == -1 || packet->transmitter_code == filterTransmitterCode) {
                if(verbose) {
                    fprintf(stderr, "transmitter code: %d: %s\n", packet->transmitter_code,
                        packet->on_off ? "ON" : "OFF");
                }
                
                if(metricName) {
                    send_statsd_gauge(metricName);
                }
            }
        } else {
            fprintf(stderr, "decoding error\n");
        }
            
        fflush(stdin);
        free(packet);
        free(burst);
    }
    
    free(data);
    close(sock);
    return 0;
}
