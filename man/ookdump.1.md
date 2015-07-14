% ookd(1)
% Jim Studt
% June 12, 2015

# NAME

ookdump - translate ookd messages into human readable form

# SYNOPSIS

ookdump [*-a address*] [*-p port*]...

# DESCRIPTION

Ookdump listens for ookd messages on a multicast port and prints a human
readable dump of the messages to standard output. Once started it runs
until interrupted.

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
