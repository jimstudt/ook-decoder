package ook

import (
	"log"
)

func Quantify( burst *Burst) {
	bins := 4

	highs := make( []int, len(burst.Pulses) )
	lows := make( []int, len(burst.Pulses) )

	for i,p := range burst.Pulses {
		highs[i] = int(p.High)
		lows[i] = int(p.Low)
	}

	QuantifySequence( highs, bins)
	QuantifySequence( lows, bins)
}

func rangeOf( seq []int) (int,int) {
	min := seq[0]
	max := seq[0]

	for _,v := range(seq) {
		if v > max {
			max = v
		}
		if v < min {
			min = v
		}
	}
	return min,max
}

func QuantifySequence( seq []int, binCount int ) {
	min,max := rangeOf(seq)
	max += 1

	log.Printf("range is %d to %d", min,max)

	type bin struct {
		left int   // inclusive
		right int  // exclusive
		center int
		count int
		sum int
		badness int
		previousCount int
	}
	
	bins := make( []bin, binCount)
	
	for i,_ := range(bins) {
		bins[i].left = min + (max-min)*i/binCount
		bins[i].right = min + (max-min)*(i+1)/binCount
		bins[i].center = (bins[i].left + bins[i].right + 1)/2
	}

	iabs := func(i int) int{
		if i < 0 { return -i }
		return i
	}

	for {
		// prepare for new run
		for i,_ := range(bins) {
			log.Printf("bin %d --- %5d .. %5d .. %5d    = %5d", i, bins[i].left, bins[i].center,bins[i].right, bins[i].count)
			bins[i].previousCount = bins[i].count
			bins[i].count = 0
			bins[i].sum = 0
			bins[i].badness = 0
		}

		// put each element of the sequence into its bin
		for _,s := range(seq) {
			for i,_ := range(bins) {
				if s >= bins[i].left && s < bins[i].right {
					bins[i].count += 1
					bins[i].sum += s
					bins[i].badness += iabs( s - bins[i].center )
					break;
				}
			}
		}

		// compute new centers
		for i,_ := range(bins) {
			if bins[i].count != 0 {
				bins[i].center = bins[i].sum / bins[i].count
			}
		}
		// update the edges (except first and last of course)
		for i := 1; i < len(bins); i++ {
			e := (bins[i-1].center + bins[i].center)/2
			bins[i].left = e
			bins[i-1].right = e
		}

		// count how many bins changed population counts
		stable := 0
		for i,_ := range(bins) {
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
