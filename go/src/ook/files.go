package ook

import (
	"archive/tar"
	"fmt"
	"io"
	"time"
)

type Reader interface {
	Read() ( *Burst, error)
	Close()
}

type Writer interface {
	Write( *Burst) error
	Close()
}

type tarWriter struct {
	sequence int
	w *tar.Writer
}

func NewTarWriter( sink io.Writer) Writer {
	return &tarWriter{ 
		w: tar.NewWriter(sink),
		sequence: 1,
	}
}

func (tw *tarWriter) Write( burst *Burst) error {
	encoded,err := burst.Encode( )
	if err != nil {
		return fmt.Errorf("Failed to encode burst: %s", err.Error())
	}
	
	head := tar.Header{ 
		Name: fmt.Sprintf("%04d-%#v.burst", tw.sequence, burst.Position),
		Mode: 0775,
		Size: int64(len(encoded)),
		ModTime: time.Now(),
		Typeflag: tar.TypeReg,
	}
	tw.sequence = tw.sequence + 1
	if err := tw.w.WriteHeader(&head); err != nil {
		return fmt.Errorf("Failed to write tar header: %s", err.Error())
	}
	n,err := tw.w.Write( encoded)
	if err != nil {
		return fmt.Errorf("Failed to write tar contents: %s", err.Error())
	}
	if n != len(encoded) {
		return fmt.Errorf("Got wrong length written to tar file")
	}
	return nil
}

func (tw *tarWriter) Close() {
	tw.w.Close()
}


type tarReader struct {
	r *tar.Reader
}

func (tr *tarReader) Read () (*Burst, error) {
	header,err := tr.r.Next()
	if err != nil {
		return nil,err
	}

	buf := make( []byte, header.Size )
	
	n,err := tr.r.Read( buf)
	if n != int(header.Size) {
		return nil,fmt.Errorf("Short read in tarReader")
	}
	if err != nil {
		return nil,err
	}

	burst,used,err := DecodeBurst(buf)
	if err != nil {
		return nil,err
	}
	_ = used

	return burst,nil
}

func (tr *tarReader) Close() {
	// odd, there isn't a close on tar.Reader.
	// I guess we have to read the whole thing and throw it away.
	// worry about that later
}

func OpenFile( source io.Reader) (Reader,error) {
	return &tarReader{ r: tar.NewReader(source) }, nil
}
