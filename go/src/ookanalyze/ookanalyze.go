package main

import (
	"flag"
	"io"
	"log"
	"ook"
	"os"
)

var verbose = flag.Bool("verbose", false, "be verbose")
var input = flag.String("input", "-", "path to source file")

func main() {
	flag.Parse()

	sourceFile := func() *os.File {
		if *input == "-" {
			return os.Stdin
		} else {
			s, err := os.Open(*input)
			if err != nil {
				log.Fatalf("Failed to open input file (%s): %s", *input, err.Error())
			}
			return s
		}
		return os.Stdin // not really reached, go 1.1 requires this. sad.
	}()

	source, err := ook.OpenFile(sourceFile)
	if err != nil {
		log.Fatalf("Unable to open %s: %s", *input, err.Error())
	}
	defer source.Close()

	for {
		burst, err := source.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			log.Fatalf("Error reading burst from input: %s", err.Error())
		}
		if *verbose {
			log.Printf("read burst: %d pulses, offset=%dHz", len(burst.Pulses), burst.Pulses[0].Frequency)
		}
		ook.Quantify(burst)
	}
}
