package kd

import "testing"

// lcgTie is a small deterministic PRNG used only to reproduce the tie-break
// regression below -- kept separate from kd_test.go's own RNG helpers.
type lcgTie struct{ state uint32 }

func (r *lcgTie) next() int32 {
	r.state = r.state*1664525 + 1013904223
	return int32(r.state >> 16)
}

func (r *lcgTie) nextRange(max int32) int32 {
	v := r.next() % max
	if v < 0 {
		v += max
	}
	return v
}

// TestTieBreakRegression guards against a bug where HardDelete's
// promote-and-cascade step could swap a different item into a node's tree
// position, changing the "other axis" values used to break an exact
// coordinate tie -- silently misrouting later searches for an unrelated,
// never-deleted item. Interleaving inserts and hard-deletes at this seed
// reliably reproduced the bug before findItem/hardDeleteRecursive were
// fixed to check (read-only) which side actually holds an item on a tie,
// instead of guessing from the current node's other axes.
func TestTieBreakRegression(t *testing.T) {
	const n = 12000
	rng := &lcgTie{state: 42}
	randBox := func() Box[int32] {
		left := rng.nextRange(4001) - 2000
		bottom := rng.nextRange(4001) - 2000
		floor := rng.nextRange(4001) - 2000
		return Box[int32]{left, bottom, floor, left + rng.nextRange(50), bottom + rng.nextRange(50), floor + rng.nextRange(50)}
	}

	tree := Create[int32]()
	boxes := make([]Box[int32], n)
	deleted := make([]bool, n)

	for i := 0; i < n; i++ {
		b := randBox()
		boxes[i] = b
		tree.Insert(i, b)

		if i%3 == 0 && i > 0 {
			victim := int(rng.nextRange(int32(i)))
			if !deleted[victim] {
				if tree.HardDelete(victim, boxes[victim]) {
					deleted[victim] = true
				}
			}
		}

		for j := 0; j <= i; j++ {
			if deleted[j] {
				continue
			}
			if !tree.IsMember(j, boxes[j]) {
				t.Fatalf("item %d box=%v unfindable at step i=%d (count=%d)", j, boxes[j], i, tree.Count())
			}
		}
	}
}
