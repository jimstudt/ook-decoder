% ookd(1)
% Jim Studt
% June 12, 2015

# NAME

oregonsci - recieve Oregon Scientific weather station data from ookd

# SYNOPSIS

oregonsci [*-a address*] [*-p port*]...

# DESCRIPTION

Oregonsci listens for ookd messages on a multicast port, decodes them
as Oregon Scientific weather station data, and stores the data in
JSON files for other programs to use.

The units are unix time, celcius, meters per second, degrees of angle,
and percent relative humidity.

It stores the most recent observations in a file with a static name,
the defaults is `/tmp/current-weather.json`.

An example static file is:

````
$ cat /tmp/current-weather.json
{
    "temperature":16.1,
    "humidity":67.0,
    "avgWindSpeed":2.1,
    "gustSpeed":2.6,
    "rainfall":0.0000,
    "batteryLow":0,
    "windDirection":337
}
````

Periodically, it stores a summary of the interval in a timestamped
file, the default is `/tmp/weather-YYYYMMDD-HHMMSS.json` where the
capital letters are replaced with the current timestamp. The default
interval is 5 minutes.

The periodic file contains for each sample type the number of samples
summed, the sum, the sum of the squares, the minimum and the
maximum. You can use the sum of the squares to calculate a standard
deviation if you like.

An example periodic file is:
````
$ cat /tmp/weather-20150616-150620.json
{
    "start":1434467180,
    "end":1434467498,
    "temperature" : { "n":7, "sum":116, "sum2":1909, "min":16.4,"max":16.6 },
    "humidity" : { "n":7, "sum":468, "sum2":3.13e+04, "min":65, "max":68},
    "avgWindSpeed" : { "n":0, "sum":0, "sum2":0, "min":0, "max":0 },
    "gustSpeed" : { "n":0, "sum":0, "sum2":0, "min":0, "max":0 },
    "rainfall" : { "n":6, "sum":0, "sum2":0, "min":0, "max":0 },
    "batteryLow" : { "n":0, "sum":0, "sum2":0, "min":0, "max":0 },
    "windDirection" : { "n":0, "sum":0, "sum2":0, "min":0, "max":0 }
}
````

# OPTIONS

-a *ADDRESS*, \--multicast-address *ADDRESS*
:   The multicast address to listen on.
    The default is 236.0.0.1.

-p *PORT*, \--multicast-port *PORT*
:   The port to to listen on. The default is 3636.

-i *ADDRESS*, \--multicast-interface *ADDRESS*
:   The address of the interface to listen on.
    The default is 127.0.0.1 which will only see them from the local computer. If
    you wish to see packets from other computers you will need to use
    an external IP addresses.

-v, \--verbose
:   Print verbose information while working.

-h, -?, \--help
:   Print usage information.

# SEE ALSO

`ookd` (1)

# WWW

ookd is maintained at <http://github.com/jimstudt/ook-decoder/>.
