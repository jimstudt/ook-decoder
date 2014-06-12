package main

import (
	"flag"
	"io"
	"log"
	"net"
	"ook"
	"os"
	"os/signal"
	"syscall"
)

var address = flag.String("address", "236.0.0.1:3636", "multicast to this address")
var ifaceName = flag.String("interface", "lo", "interface on which to multicast")
var verbose = flag.Bool("verbose", false, "be verbose")
var input = flag.String("input", "-", "path to file of bursts")

// Get an IPv4 address for a given interface.
func interfaceIP4(iface *net.Interface) net.IP {
	addrs, err := iface.Addrs()
	if err != nil {
		return nil
	}
	for _, n := range addrs {
		ip, _, err := net.ParseCIDR(n.String())
		if err == nil {
			if ip4 := ip.To4(); ip4 != nil {
				return ip4
			}
		}
	}
	return nil
}

func main() {
	flag.Parse()

	terminate := make(chan os.Signal)
	signal.Notify(terminate, syscall.SIGINT)

	udpAddr, err := net.ResolveUDPAddr("udp", *address)
	if err != nil {
		log.Fatalf("Unable to resolve multicasting address (%s): %s", *address, err.Error())
	}
	if *verbose {
		log.Printf("Multicasting to %s", udpAddr.String())
	}

	iface, err := net.InterfaceByName(*ifaceName)
	if err != nil {
		log.Fatalf("Unknown interface (%s): %s", *ifaceName, err.Error())
	}

	ifaceAddr := interfaceIP4(iface)
	if ifaceAddr == nil {
		log.Fatalf("Could not find an IPv4 address on the interface")
	}

	localAddr := net.UDPAddr{IP: ifaceAddr, Port: 0}

	udpConn, err := net.DialUDP("udp4", &localAddr, udpAddr)
	if err != nil {
		log.Fatalf("Failed to dial UDP: %s", err.Error())
	}

	source := func() *os.File {
		if *input == "-" {
			return os.Stdin
		} else {
			s, err := os.Open(*input)
			if err != nil {
				log.Fatalf("Failed to open input file (%s): %s", *input, err.Error())
			}
			return s
		}
	}()

	reader, err := ook.OpenFile(source)
	if err != nil {
		log.Fatalf("Failed to open input source: %s", err.Error())
	}

	for {
		burst, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			log.Fatalf("Error reading burst: %s", err.Error())
		}
		log.Printf("Read %d pulses", len(burst.Pulses))

		bytes, err := burst.Encode()
		if err != nil {
			log.Fatalf("Failed to encode burst: %s", err.Error())
		}
		log.Printf("Encodes to %d bytes", len(bytes))

		c, err := udpConn.Write(bytes)
		if err != nil {
			log.Fatalf("Failed to write burst (%s -> %s): %s", localAddr.String(), udpAddr.String(), err.Error())
		}
		if c != len(bytes) {
			log.Fatalf("Truncated burst %d != %d", c, len(bytes))
		}
	}
}
