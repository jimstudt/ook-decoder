package ook

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"time"
)


type Pulse struct {
	High uint32
	Low uint32
	Frequency int32
}

type Burst struct {
	Position time.Duration
	Pulses []Pulse
}


type BurstHandler func( *Burst) bool

func EncodeBurst( burst *Burst) ([]byte, error) {
	buf := bytes.NewBuffer( []byte{} )

	version := uint32( 0x36360001)
	position := uint64(burst.Position)
	count := uint32( len( burst.Pulses))
	
	if err := binary.Write( buf, binary.LittleEndian, &version); err != nil {
		return []byte{}, err
	}
	if err := binary.Write( buf, binary.LittleEndian, &position); err != nil {
		return []byte{}, err
	}
	if err := binary.Write( buf, binary.LittleEndian, &count); err != nil {
		return []byte{}, err
	}

	for _,p := range burst.Pulses {
		hi := uint32( p.High)
		lo := uint32( p.Low)
		freq := int32( p.Frequency)

		if err := binary.Write( buf, binary.LittleEndian, &hi); err != nil {
			return []byte{}, err
		}
		if err := binary.Write( buf, binary.LittleEndian, &lo); err != nil {
			return []byte{}, err
		}
		if err := binary.Write( buf, binary.LittleEndian, &freq); err != nil {
			return []byte{}, err
		}
	}

	return buf.Bytes(), nil
}

func DecodeBurst( data []byte ) (*Burst,int,error) {
	version := uint32(0)
	position := uint64(0)
	count := uint32(0)
	
	buf := bytes.NewBuffer(data)
	
	if err := binary.Read( buf, binary.LittleEndian, &version); err != nil {
		return nil,0,err
	}
	if version != uint32( 0x36360001) {
		return nil,0,fmt.Errorf("Bad version in burst packet")
	}

	if err := binary.Read( buf, binary.LittleEndian, &position); err != nil {
		return nil,0,err
	}
	if err := binary.Read( buf, binary.LittleEndian, &count); err != nil {
		return nil,0,err
	}

	log.Printf("pos=%v count=%v", position, count)

	pulses := make( []Pulse, 0, count)
	for i := 0; i < int(count); i++ {
		hi := uint32(0)
		lo := uint32(0)
		freq := int32(0)

		if err := binary.Read( buf, binary.LittleEndian, &hi); err != nil {
			return nil,0,err
		}
		if err := binary.Read( buf, binary.LittleEndian, &lo); err != nil {
			return nil,0,err
		}
		if err := binary.Read( buf, binary.LittleEndian, &freq); err != nil {
			return nil,0,err
		}

		pulses = append( pulses, Pulse{ High:hi, Low:lo, Frequency:freq} )
	}

	// need to say how much we used
	return &Burst{ Position:time.Duration(position), Pulses:pulses},0,nil
}

func ListenTo( iface *net.Interface, addr *net.UDPAddr, handler BurstHandler) error {
	conn,err := net.ListenMulticastUDP("udp", iface, addr)
	if err != nil {
		return err
	}
	defer conn.Close()

	buf := make( []byte, 65536)

	for {
		count,err := conn.Read(buf)
		if err != nil {
			return err

		}

		burst,used,err := DecodeBurst( buf[0:count] )
		if err != nil {
			return err
		}
		_ = used 
		
		if ok := handler( burst); !ok {
			break;
		}
		
	}

	return nil
}



