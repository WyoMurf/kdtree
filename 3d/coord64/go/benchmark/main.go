package main

import (
	"fmt"
	"math/rand"
	"time"
	"github.com/WyoMurf/kdtree/3d/coord64/go"
)

func main() {
	tree := kd.Create()
	rng := rand.New(rand.NewSource(42))

	nBoxes := 1000000
	boxes := make([]kd.Box, nBoxes)

	fmt.Printf("Inserting %d boxes...\n", nBoxes)
	start := time.Now()
	for i := 0; i < nBoxes; i++ {
		x1 := rng.Int63n(10000000000)
		y1 := rng.Int63n(10000000000)
		z1 := rng.Int63n(10000000000)
		x2 := x1 + int64(rng.Intn(100)) + 1
		y2 := y1 + int64(rng.Intn(100)) + 1
		z2 := z1 + int64(rng.Intn(100)) + 1
		
		boxes[i] = kd.Box{x1, y1, z1, x2, y2, z2}
		tree.Insert(i+1, boxes[i])
	}
	fmt.Printf("Insertion took %v\n", time.Since(start))

	searchArea := kd.Box{0, 0, 0, 5000000000, 5000000000, 5000000000}
	fmt.Printf("Searching in area %v...\n", searchArea)
	start = time.Now()
	gen := tree.Start(searchArea)
	foundCount := 0
	for {
		_, _, ok := gen.Next()
		if !ok {
			break
		}
		foundCount++
	}
	fmt.Printf("Found %d boxes in search area. Search took %v\n", foundCount, time.Since(start))

	fmt.Println("Deleting 1000 items...")
	start = time.Now()
	for i := 0; i < 1000; i++ {
		tree.HardDelete(i+1, boxes[i])
	}
	fmt.Printf("Deletion took %v\n", time.Since(start))
}
