% ookd(1)
% Jim Studt
% June 12, 2015

# NAME

ookd - on-off keying decoder

# SYNOPSIS

ookd [*-f frequency*]...

# DESCRIPTION

Ookd bridges broadcast radio telemetry data to internet multicast
addresses.

Ookd listen to a software defined radio using librtlsdr, recognizes an
on off keyed signal, and broadcasts a description of the signal onto
an IP network in a defined binary format.

You will use ookd in conjunction with one or more clients which
receive the pulse data from the internet multicast messages and
interpret them for a specific device, e.g. a weather station,
thermometer, or alarm device.

# OPTIONS

-f *FREQUENCY*, \--frequency *FREQUENCY*
:   Specify the center frequency of the carrier in hertz. The default is
    433910000. Signals some way to each side are still received, and
    the actual frequency of each pulse is recorded along with its
    length to facilitate disambiguation of sources.

-m *NUM*, \--min-packet *NUM*
:   The minimum number of pulses required to make a packet. This is used
    to avoid sending large numbers of packets for radio noise. The default
    is 10.

-a *ADDRESS*, \--multicast-address *ADDRESS*
:   The multicast address where the recognized packets are broadcast.
    The default is 236.0.0.1.

-p *PORT*, \--multicast-port *PORT*
:   The port to which recognized packets are sent. The default is 3636.

-i *ADDRESS*, \--multicast-interface *ADDRESS*
:   The address of the interface where recognized packets will be multicast.
    The default is 127.0.0.1 which will confine them to the local computer. If
    you wish to send them out of the computer you will need to use one of your
    external IP addresses.

-r *FILENAME*, \--read-file *FILENAME*
:   The name of a file to read the IQ raw data instead of from a radio.
    This is useful for testing and tuning.

-v, \--verbose
:   Print verbose information while working.

-h, -?, \--help
:   Print usage information.

# SEE ALSO

`ookdump` (1), `wh1080` (1), `oregonsci` (1), `ws2300` (1), `nexa` (1)

# WWW

ookd is maintained at <http://github.com/jimstudt/ook-decoder/>.
