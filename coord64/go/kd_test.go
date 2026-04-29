package kd

import (
	"fmt"
	"math/rand"
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
		x1 := rng.Int63n(10000000000)
		y1 := rng.Int63n(10000000000)
		x2 := x1 + rng.Int63n(100) + 1
		y2 := y1 + rng.Int63n(100) + 1
		b := Box{x1, y1, x2, y2}
		boxes = append(boxes, b)
		tree.Insert(fmt.Sprintf("box%d", i), b)
	}

	if count := tree.Count(); count != 1000 {
		t.Fatalf("Expected 1000 boxes, got %d", count)
	}

	searchArea := Box{0, 0, 5000000000, 5000000000}
	gen := tree.Start(searchArea)
	foundCount := 0
	for {
		_, _, ok := gen.Next()
		if !ok {
			break
		}
		foundCount++
	}
	t.Logf("Found %d boxes in the large search area", foundCount)
}

func TestMillionBoxes(t *testing.T) {
	tree := Create()
	rng := rand.New(rand.NewSource(42))

	var boxesToDelete []Box
	for i := 0; i < 1000000; i++ {
		x1 := rng.Int63n(10000000000)
		y1 := rng.Int63n(10000000000)
		x2 := x1 + rng.Int63n(100) + 1
		y2 := y1 + rng.Int63n(100) + 1
		b := Box{x1, y1, x2, y2}
		
		if i < 1000 {
			boxesToDelete = append(boxesToDelete, b) // Store first 1,000 for hard deletion
		}
		tree.Insert(fmt.Sprintf("box%d", i), b)
	}

	if count := tree.Count(); count != 1000000 {
		t.Fatalf("Expected 1000000 boxes, got %d", count)
	}

	searchArea := Box{0, 0, 5000000000, 5000000000}
	gen := tree.Start(searchArea)
	foundCount := 0
	for {
		_, _, ok := gen.Next()
		if !ok {
			break
		}
		foundCount++
	}
	t.Logf("Found %d boxes in the 0-5,000,000,000 search area", foundCount)

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
