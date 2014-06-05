package main

import (
	"flag"
	"log"
	"net"
	"ook"
	"os"
	"os/signal"
	"syscall"
)

var address = flag.String("address", "236.0.0.1:3636", "listen to this address")
var ifaceName = flag.String("interface", "lo", "interface on which to listen")
var verbose = flag.Bool("verbose", false, "be verbose")
var output = flag.String("output", "-", "path to write output")

func main() {
	flag.Parse()

	terminate := make(chan os.Signal)
	signal.Notify(terminate, syscall.SIGINT)

	udpAddr, err := net.ResolveUDPAddr("udp", *address)
	if err != nil {
		log.Fatalf("Unable to resolve listenting address (%s): %s", *address, err.Error())
	}
	if *verbose {
		log.Printf("Listening to %s", udpAddr.String())
	}

	iface,err := net.InterfaceByName( *ifaceName)
	if err != nil {
		log.Fatalf("Unknown interface (%s): %s", *ifaceName, err.Error())
	}

	sink := func() *os.File {
		if *output == "-" {
			return os.Stdout
		} else {
			s, err := os.Create(*output)
			if err != nil {
				log.Fatalf("Failed to create output file (%s): %s", *output, err.Error())
			}
			return s
		}
		return os.Stdout // not really reached. very sad.  go 1.1 requires this
	}()

	writer := ook.NewTarWriter(sink)
	defer writer.Close()

	incoming := make(chan *ook.Burst, 16)

	handler := func(burst *ook.Burst) bool {
		if *verbose {
			log.Printf("got a burst")
		}

		err := writer.Write(burst)
		if err != nil {
			log.Fatalf("Failed to write burst: %s", err.Error())
		}
		return true
	}

	if err := ook.ListenTo(iface, udpAddr, incoming); err != nil {
		log.Fatalf("Failed to listen to address: %s", err.Error())
	}

	done := false

	for !done {
		select {
		case burst := <-incoming:
			handler(burst)
		case _ = <-terminate:
			done = true
		}
	}
}
