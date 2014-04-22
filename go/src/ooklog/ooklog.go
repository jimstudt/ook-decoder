package main

import (
	"archive/tar"
	"flag"
	"fmt"
	"log"
	"net"
	"ook"
	"os"
	"os/signal"
	"syscall"
	"time"
)

var address = flag.String("address", "236.0.0.1:3636", "listen to this address")
var ifaceName = flag.String("interface", "lo", "interface on which to listen")
var verbose = flag.Bool("verbose", false, "be verbose")
var output = flag.String("output", "-", "path to write output")

func main() {
	flag.Parse()

	terminate := make( chan os.Signal)
	signal.Notify( terminate, syscall.SIGINT)

	udpAddr,err := net.ResolveUDPAddr( "udp", *address)
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
			s,err := os.Create(*output)
			if err != nil {
				log.Fatalf("Failed to create output file (%s): %s", *output, err.Error())
			}
			return s
		}
	}()

	tarfile := tar.NewWriter(sink)
	defer tarfile.Close()

	sequence := 1

	incoming := make( chan *ook.Burst, 16)

	handler := func( burst *ook.Burst) bool {
		log.Printf("got a burst")

		encoded,err := burst.Encode( )
		if err != nil {
			log.Fatalf("Failed to encode burst: %s", err.Error())
		}

		head := tar.Header{ 
			Name: fmt.Sprintf("%04d-%#v.burst", sequence, burst.Position),
			Mode: 0775,
			Size: int64(len(encoded)),
			ModTime: time.Now(),
			Typeflag: tar.TypeReg,
		}
		if err := tarfile.WriteHeader(&head); err != nil {
			log.Fatal("Failed to write tar header: %s", err.Error())
		}
		n,err := tarfile.Write( encoded)
		if err != nil {
			log.Fatal("Failed to write tar contents: %s", err.Error())
		}
		if n != len(encoded) {
			log.Fatal("Got wrong length written to tar file")
		}
		return true
	}

	if err := ook.ListenTo( iface, udpAddr, incoming); err != nil {
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
