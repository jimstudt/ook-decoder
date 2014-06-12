package ook

import ()

type BitStream struct {
	bits []byte // one bit per byte to make things easy
}

type BitStreamReader struct {
	stream *BitStream
	thumb  int
}

func cleanBit(b byte) int {
	if b == 0 {
		return 0
	} else {
		return 1
	}
}

func nibbleLSB(b []byte, t int) int {
	return cleanBit(b[t]) + 2*cleanBit(b[t+1]) + 4*cleanBit(b[t+2]) + 8*cleanBit(b[t+3])
}

func NewBitStream(sizedFor int) *BitStream {
	return &BitStream{
		bits: make([]byte, 0, sizedFor),
	}
}

func (bs *BitStream) Add(v int) {
	if v != 0 && v != 1 {
		panic("illegal bit")
	}
	bs.bits = append(bs.bits, byte(v))
}

func (bs *BitStream) Reader() *BitStreamReader {
	return &BitStreamReader{
		stream: bs,
	}
}

func (bs *BitStreamReader) EOF() bool {
	return bs.thumb >= len(bs.stream.bits)
}

func (bs *BitStreamReader) PeekBit() int {
	if bs.EOF() {
		panic("overread")
	}
	return cleanBit(bs.stream.bits[bs.thumb])
}

func (bs *BitStreamReader) GetBit() int {
	if bs.EOF() {
		panic("overread")
	}
	bit := bs.stream.bits[bs.thumb]
	bs.thumb++
	return cleanBit(bit)
}

func (bs *BitStreamReader) UngetNBits(n int) {
	p := bs.thumb - n
	if p < 0 {
		panic("over unget")
	}
	bs.thumb = p
}

func (bs *BitStreamReader) UngetBit() {
	bs.UngetNBits(1)
}

func (bs *BitStreamReader) PeekNibbleLSB() (int, bool) {
	if bs.thumb+4 > len(bs.stream.bits) {
		return 0, false
	}
	return nibbleLSB(bs.stream.bits, bs.thumb), true
}
func (bs *BitStreamReader) GetNibbleLSB() (int, bool) {
	v, ok := bs.PeekNibbleLSB()
	if !ok {
		return 0, false
	}
	bs.thumb += 4
	return v, true
}

func (bs *BitStreamReader) UngetNibble() {
	bs.UngetNBits(4)
}

func (bsr *BitStreamReader) RemainingBits() string {
	h := ""
	for i := 0; !bsr.EOF(); i++ {
		b := bsr.GetBit()
		if b == 0 {
			h += "0"
		} else if b == 1 {
			h += "1"
		} else {
			h += "?"
		}
		if i%4 == 3 {
			h += " "
		}
	}
	return h
}
