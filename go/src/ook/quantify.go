package ook

import (
	"fmt"
	"log"
	"sort"
)

type Symbol int

const (
	Spurious Symbol = iota
	HighShort
	HighLong
	LowShort
	LowLong
	EndOfTransmission
)

var symbolNames = map[Symbol]string{
	Spurious:          "?",
	HighShort:         "-",
	HighLong:          "--",
	LowShort:          "_",
	LowLong:           "__",
	EndOfTransmission: ".",
}

func (s Symbol) String() string {
	v, ok := symbolNames[s]
	if ok {
		return v
	} else {
		return "!"
	}
}

type ClusterDescription struct {
	Min   int
	Max   int
	Count int
	Sum   int
	Sum2  int
	Sym   Symbol
}
type ClusterDescriptions []*ClusterDescription

func AssignSymbols(highs, lows ClusterDescriptions) {
	if len(highs) == 2 {
		highs[0].Sym = HighShort
		highs[1].Sym = HighLong
	}
	if len(lows) == 3 {
		lows[0].Sym = LowShort
		lows[1].Sym = LowLong
		if lows[2].Count == 1 {
			lows[2].Sym = EndOfTransmission
		}
	}
}

func (s ClusterDescriptions) Lookup(duration int) *ClusterDescription {
	for _, c := range s {
		if duration >= c.Min && duration <= c.Max {
			return c
		}
	}
	return nil
}

func CombineClusters(a, b *ClusterDescription) *ClusterDescription {
	r := ClusterDescription{
		Count: a.Count + b.Count,
		Sum:   a.Sum + b.Sum,
		Sum2:  a.Sum2 + b.Sum2,
	}
	if a.Min < b.Min {
		r.Min = a.Min
	} else {
		r.Min = b.Min
	}

	if a.Max > b.Max {
		r.Max = a.Max
	} else {
		r.Max = b.Max
	}

	return &r
}

func FoldLeadingRunt(s ClusterDescriptions, firstLength int) ClusterDescriptions {
	if len(s) < 2 {
		return s
	}

	for i, c := range s {
		if c.Min > firstLength {
			break
		}
		if c.Count == 1 && c.Min == firstLength && i+1 < len(s) {
			r := []*ClusterDescription{}
			if i > 0 {
				r = append(r, s[0:i]...)
			}
			r = append(r, CombineClusters(s[i], s[i+1]))
			r = append(r, s[i+2:]...)
			return r
		}
	}
	return s
}

type byMin ClusterDescriptions

func (a byMin) Len() int           { return len(a) }
func (a byMin) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a byMin) Less(i, j int) bool { return a[i].Min < a[j].Min }

func Quantify(burst *Burst) {
	//bins := 4

	highs := make([]int, len(burst.Pulses))
	lows := make([]int, len(burst.Pulses))

	for i, p := range burst.Pulses {
		highs[i] = int(p.High)
		lows[i] = int(p.Low)
	}

	//	LLoyds(highs, bins)
	//LLoyds(lows, bins)

	tolerance := 0.2
	verbose := false
	highClusters := GuessAndGrow(highs, tolerance, verbose)
	highClusters = FoldLeadingRunt(highClusters, int(burst.Pulses[0].High))
	lowClusters := GuessAndGrow(lows, tolerance, verbose)

	AssignSymbols(highClusters, lowClusters)

	for n, c := range highClusters {
		s := ""
		if c.Count == 1 {
			s += " single"
			if c.Min == highs[0] {
				s += " first"
			}
			if c.Min == highs[len(highs)-1] {
				s += " last"
			}
		}
		log.Printf("high %d - %d..%d%s", n, c.Min, c.Max, s)
	}
	for n, c := range lowClusters {
		s := ""
		if c.Count == 1 {
			s += " single"
			if c.Min == lows[0] {
				s += " first"
			}
			if c.Min == lows[len(lows)-1] {
				s += " last"
			}
		}
		log.Printf("low %d - %d..%d%s", n, c.Min, c.Max, s)
	}

	syms := make([]Symbol, 0, len(burst.Pulses)*2)

	v := ""
	for _, p := range burst.Pulses {
		ch := highClusters.Lookup(int(p.High))
		cl := lowClusters.Lookup(int(p.Low))

		syms = append(syms, ch.Sym)
		syms = append(syms, cl.Sym)

		v = v + ch.Sym.String() + cl.Sym.String()
	}
	log.Printf(":: %s", v)

	decoded, err := DecodeManchester(syms)
	if err != nil {
		log.Printf("Manchester decode failed: %s", err.Error())
	} else {
		log.Printf("machester: %s", decoded.Reader().RemainingBits())

		n := ""
		nr := decoded.Reader()
		for !nr.EOF() {
			nibble, ok := nr.GetNibbleLSB()
			if ok {
				n = n + fmt.Sprintf("%x", nibble)
			} else {
				n = n + "+b:" + nr.RemainingBits()
			}
		}
		log.Printf("manchester: %s", n)
	}

}

func rangeOf(seq []int) (int, int) {
	min := seq[0]
	max := seq[0]

	for _, v := range seq {
		if v > max {
			max = v
		}
		if v < min {
			min = v
		}
	}
	return min, max
}

func summarizeSamples(seq []int) string {
	if len(seq) == 0 {
		return "no samples"
	}

	sum := 0
	sum2 := 0
	max := seq[0]
	min := seq[0]

	for _, v := range seq {
		sum += v
		sum2 += v * v
		if v > max {
			max = v
		}
		if v < min {
			min = v
		}
	}

	return fmt.Sprintf("%d samples center=%d width=%d %d...%d", len(seq), sum/len(seq), max-min+1, min, max)
}

type action byte

const (
	errorAction action = iota
	emitZeroAction
	emitOneAction
	noAction
	endAction
)

type state byte

const (
	d0 state = 8 * iota
	d1
	c0
	c1
)

type actionNext struct {
	act  action
	next state
}

var machine = [4 * 8]actionNext{
	byte(d1) + byte(LowShort):          actionNext{noAction, c0},
	byte(d1) + byte(LowLong):           actionNext{emitZeroAction, d0},
	byte(d1) + byte(EndOfTransmission): actionNext{endAction, c0},
	byte(c0) + byte(HighShort):         actionNext{emitOneAction, d1},

	byte(d0) + byte(HighShort):         actionNext{noAction, c1},
	byte(d0) + byte(HighLong):          actionNext{emitOneAction, d1},
	byte(c1) + byte(LowShort):          actionNext{emitZeroAction, d0},
	byte(c1) + byte(EndOfTransmission): actionNext{endAction, c0},
}

func DecodeManchester(syms []Symbol) (*BitStream, error) {
	bits := NewBitStream(len(syms))
	done := false

	st := c0
	for _, sym := range syms {
		if done {
			return nil, fmt.Errorf("Symbols after EndOfTransmission")
		}
		idx := byte(st) + byte(sym)
		switch machine[idx].act {
		case errorAction:
			return nil, fmt.Errorf("Invalid manchester encoding: state=%d sym=%d %#v",
				st, sym, bits)
		case emitZeroAction:
			bits.Add(0)
		case emitOneAction:
			bits.Add(1)
		case endAction:
			done = true
		}
		st = machine[idx].next
	}

	return bits, nil
}

func GuessAndGrow(seqUnsorted []int, tolerance float64, verbose bool) ClusterDescriptions {
	pool := make([]int, 0, len(seqUnsorted))
	pool = append(pool, seqUnsorted...)
	sort.Ints(pool)

	if verbose {
		log.Printf("GuessAndGrow, tolerance=%f", tolerance)
	}

	clusters := ClusterDescriptions{}

	grow := func(left int, right int, sum int, sum2 int, count int) (int, int, int, int, int) {
		min := int(float64(pool[left]) * (1.0 - tolerance))
		max := int(float64(pool[right]) * (1.0 + tolerance))

		for i := left - 1; i >= 0 && pool[i] >= min; i-- {
			sum += pool[i]
			sum2 += pool[i] * pool[i]
			left = i
		}

		for i := right + 1; i < len(pool) && pool[i] <= max; i++ {
			sum += pool[i]
			sum2 += pool[i] * pool[i]
			right = i
		}

		return left, right, sum, sum2, right - left + 1
	}

	for len(pool) > 0 {
		center := len(pool) / 2
		cv := pool[center]

		left, right, sum, sum2, count := grow(center, center, cv, cv*cv, 1)
		if verbose {
			log.Printf("  pass1 %s", summarizeSamples(pool[left:right+1]))
		}

		for pass := 2; ; pass++ {
			previousCount := count
			left, right, sum, sum2, count = grow(left, right, sum, sum2, count)
			if count == previousCount {
				break
			}
			if verbose {
				log.Printf("  pass(%d) %s", pass, summarizeSamples(pool[left:right+1]))
			}
		}

		clusters = append(clusters, &ClusterDescription{
			Min:   pool[left],
			Max:   pool[right],
			Count: count,
			Sum:   sum,
			Sum2:  sum2,
		})

		pool = append(pool[0:left], pool[right+1:]...)
	}

	sort.Sort(byMin(clusters))
	return clusters
}

func LLoyds(seqUnsorted []int, binCount int) {
	seq := make([]int, 0, len(seqUnsorted))
	seq = append(seq, seqUnsorted...)
	sort.Ints(seq)

	min, max := rangeOf(seq)
	max += 1

	log.Printf("Lloyds %d samples into %d bins", len(seq), binCount)
	log.Printf("range is %d to %d", min, max)

	type bin struct {
		left          int // inclusive
		right         int // exclusive
		center        int
		count         int
		sum           int
		badness       int
		min           int
		max           int
		previousCount int
	}

	bins := make([]bin, binCount)

	for i, _ := range bins {
		bins[i].left = min + (max-min)*i/binCount
		bins[i].right = min + (max-min)*(i+1)/binCount
		bins[i].center = (bins[i].left + bins[i].right + 1) / 2
	}

	iabs := func(i int) int {
		if i < 0 {
			return -i
		}
		return i
	}

	firstPass := true

	for {
		if firstPass {
			// put each element of the sequence into its bin.
			// We put about an equal number of elements in each bin to start
			for nth, s := range seq {
				i := len(bins) * nth / len(seq)
				bins[i].count += 1
				bins[i].sum += s
				bins[i].badness += iabs(s - bins[i].center)
			}
			firstPass = false
		} else {
			// prepare for new run
			for i, _ := range bins {
				log.Printf("bin %d --- %7d .. %7d < %7d < %7d .. %7d    = %4d", i,
					bins[i].left, bins[i].min, bins[i].center, bins[i].max, bins[i].right, bins[i].count)
				bins[i].previousCount = bins[i].count
				bins[i].count = 0
				bins[i].sum = 0
				bins[i].badness = 0
			}

			// put each element of the sequence into its bin
			for _, s := range seq {
				for i, _ := range bins {
					if s >= bins[i].left && s < bins[i].right {
						bins[i].count += 1
						bins[i].sum += s
						bins[i].badness += iabs(s - bins[i].center)
						if s > bins[i].max {
							bins[i].max = s
						}
						if s < bins[i].min {
							bins[i].min = s
						}
						break
					}
				}

			}
		}

		// see how we are
		for i, _ := range bins {
			log.Printf("bin %d --- %7d .. %7d < %7d < %7d .. %7d    = %4d", i,
				bins[i].left, bins[i].min, bins[i].center, bins[i].max, bins[i].right, bins[i].count)
		}

		// compute new centers
		for i, _ := range bins {
			if bins[i].count != 0 {
				bins[i].center = bins[i].sum / bins[i].count
			} else {
				bins[i].center = (bins[i].left + bins[i].right) / 2
			}
			bins[i].min = bins[i].center
			bins[i].max = bins[i].center
		}
		// update the edges (except first and last of course)
		for i := 1; i < len(bins); i++ {
			e := (bins[i-1].center + bins[i].center) / 2
			bins[i].left = e
			bins[i-1].right = e
		}

		// count how many bins changed population counts
		stable := 0
		for i, _ := range bins {
			if bins[i].count == bins[i].previousCount {
				stable += 1
			}
		}

		// when no bins changed population counts, then no elements
		// moved. We are stable.
		if stable == len(bins) {
			break
		}

	}
}
