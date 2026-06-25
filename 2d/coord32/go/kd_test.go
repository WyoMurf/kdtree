package kd

import (
	"fmt"
	"math"
	"math/rand"
	"sort"
	"testing"
)

func TestKDTreeBasic(t *testing.T) {
	tree := Create()
	box1 := Box{0, 0, 10, 10}
	box2 := Box{20, 20, 30, 30}
	box3 := Box{5, 5, 15, 15}

	tree.Insert("item1", box1)
	tree.Insert("item2", box2)
	tree.Insert("item3", box3)

	tree.Delete("item1", box1)
}

func TestKDTreeHardDelete(t *testing.T) {
	tree := Create()
	box1 := Box{0, 0, 10, 10}
	box2 := Box{20, 20, 30, 30}
	box3 := Box{5, 5, 15, 15}

	tree.Insert("item1", box1)
	tree.Insert("item2", box2)
	tree.Insert("item3", box3)

	tree.HardDelete("item1", box1)
}

func TestThousandBoxes(t *testing.T) {
	tree := Create()
	rng := rand.New(rand.NewSource(42))

	var boxes []Box
	for i := 0; i < 1000; i++ {
		x1 := rng.Intn(10000)
		y1 := rng.Intn(10000)
		x2 := x1 + rng.Intn(100) + 1
		y2 := y1 + rng.Intn(100) + 1
		b := Box{x1, y1, x2, y2}
		boxes = append(boxes, b)
		tree.Insert(fmt.Sprintf("box%d", i), b)
	}

	if count := tree.Count(); count != 1000 {
		t.Fatalf("Expected 1000 boxes, got %d", count)
	}

	searchArea := Box{0, 0, 5000, 5000}
	gen := tree.Start(searchArea)
	foundCount := 0
	for {
		_, _, ok := gen.Next()
		if !ok {
			break
		}
		foundCount++
	}
	t.Logf("Found %d boxes in the 0-5000 search area", foundCount)

	for i := 0; i < 100; i++ {
		deleted := tree.HardDelete(fmt.Sprintf("box%d", i), boxes[i])
		if !deleted {
			t.Errorf("Failed to hard delete box%d", i)
		}
	}

	if count := tree.Count(); count != 900 {
		t.Fatalf("Expected 900 boxes after hard deletes, got %d", count)
	}
}

func TestMillionBoxes(t *testing.T) {
	tree := Create()
	rng := rand.New(rand.NewSource(42))

	var boxesToDelete []Box
	for i := 0; i < 1000000; i++ {
		x1 := rng.Intn(100000)
		y1 := rng.Intn(100000)
		x2 := x1 + rng.Intn(100) + 1
		y2 := y1 + rng.Intn(100) + 1
		b := Box{x1, y1, x2, y2}

		if i < 1000 {
			boxesToDelete = append(boxesToDelete, b) // Store first 1,000 for hard deletion
		}
		tree.Insert(fmt.Sprintf("box%d", i), b)
	}

	if count := tree.Count(); count != 1000000 {
		t.Fatalf("Expected 1000000 boxes, got %d", count)
	}

	// Search a quarter of the total 100000x100000 space
	searchArea := Box{0, 0, 50000, 50000}
	gen := tree.Start(searchArea)
	foundCount := 0
	for {
		_, _, ok := gen.Next()
		if !ok {
			break
		}
		foundCount++
	}
	t.Logf("Found %d boxes in the 0-50000 search area", foundCount)

	// Hard delete 1,000 boxes
	for i := 0; i < 1000; i++ {
		deleted := tree.HardDelete(fmt.Sprintf("box%d", i), boxesToDelete[i])
		if !deleted {
			t.Errorf("Failed to hard delete box%d", i)
		}
	}

	if count := tree.Count(); count != 999000 {
		t.Fatalf("Expected 999000 boxes after hard deletes, got %d", count)
	}
}

const (
	KD_BOXES_TEST   = 10000
	KD_REGIONS_TEST = 100
	MIN_RANGE_TEST  = -100000
	MAX_RANGE_TEST  = 100000
	RANGE_SPAN_TEST = MAX_RANGE_TEST - MIN_RANGE_TEST + 1
	BOX_RANGE_TEST  = 1000
)

func randBox(rng *rand.Rand) Box {
	var b Box
	b[Left] = rng.Intn(RANGE_SPAN_TEST) + MIN_RANGE_TEST
	b[Bottom] = rng.Intn(RANGE_SPAN_TEST) + MIN_RANGE_TEST
	b[Right] = b[Left] + rng.Intn(BOX_RANGE_TEST)
	b[Top] = b[Bottom] + rng.Intn(BOX_RANGE_TEST)
	return b
}

func TestReallyDelete(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box, KD_BOXES_TEST)
	tree := Create()

	for i := 0; i < KD_BOXES_TEST; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	if tree.Count() != KD_BOXES_TEST {
		t.Fatalf("[really] Expected initial count %d, got %d", KD_BOXES_TEST, tree.Count())
	}

	// Really delete everything in reverse order
	for i := KD_BOXES_TEST - 1; i >= 0; i-- {
		status, _, _ := tree.ReallyDelete(i+1, boxes[i])
		if status != OK {
			if !tree.IsMember(i+1, boxes[i]) {
				t.Fatalf("[really] Item %d not found in tree before delete", i+1)
			}
			t.Fatalf("[really] Failed to really delete item %d", i+1)
		}
	}

	if tree.Count() != 0 {
		t.Errorf("[really] Expected count 0, got %d", tree.Count())
	}

	// Verify empty
	gen := tree.Start(Box{MIN_RANGE_TEST - 1, MIN_RANGE_TEST - 1, MAX_RANGE_TEST + 1, MAX_RANGE_TEST + 1})
	if item, _, ok := gen.Next(); ok {
		t.Errorf("[really] Tree not empty, found %v", item)
	}
}

func TestBadness(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	tree := Create()

	for i := 0; i < 1000; i++ {
		b := randBox(rng)
		tree.Insert(i+1, b)
	}

	tree.Badness() // Should run and print balance stats without errors
}

func TestNearest(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box, KD_BOXES_TEST)
	tree := Create()

	for i := 0; i < KD_BOXES_TEST; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	for m := 1; m <= 20; m *= 2 {
		for q := 0; q < 50; q++ {
			qx := rng.Intn(RANGE_SPAN_TEST) + MIN_RANGE_TEST
			qy := rng.Intn(RANGE_SPAN_TEST) + MIN_RANGE_TEST

			list := tree.Nearest(qx, qy, m)
			if len(list) != m {
				t.Fatalf("[nearest] Expected %d neighbors, got %d", m, len(list))
			}

			// Verify sorted
			for i := 1; i < m; i++ {
				if list[i].Dist < list[i-1].Dist-1e-9 {
					t.Fatalf("[nearest] Results not sorted at m=%d q=%d i=%d", m, q, i)
				}
			}

			// Brute force
			bruteDists := make([]float64, KD_BOXES_TEST)
			qBox := Box{qx, qy, qx, qy}
			for i, box := range boxes {
				bruteDists[i] = math.Sqrt(kdDist(qBox, box))
			}
			sort.Float64s(bruteDists)

			if list[m-1].Dist > bruteDists[m-1]+1e-6 {
				t.Fatalf("[nearest] kd_nearest missed closer item at m=%d q=%d (kd furthest=%g, brute m-th=%g)",
					m, q, list[m-1].Dist, bruteDists[m-1])
			}
		}
	}

	// Edge case: point inside box
	qx := (boxes[0][Left] + boxes[0][Right]) / 2
	qy := (boxes[0][Bottom] + boxes[0][Top]) / 2
	list := tree.Nearest(qx, qy, 1)
	if list[0].Dist > 1e-9 {
		t.Errorf("[nearest] Edge case fail: dist=%g", list[0].Dist)
	}
}
