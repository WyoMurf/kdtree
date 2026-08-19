package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// NameEntry is one row of cities.names: geonameid, display name, population.
type NameEntry struct {
	Name       string
	Population int64
}

// LoadNames reads cities.names (tab-separated: geonameid, name, population)
// into a plain map keyed by geonameid.
//
// earth_viewer.c hand-rolls an open-addressing hash table over one
// contiguous array specifically to avoid ~170k individually malloc'd chain
// nodes scattered across the heap - profiling there showed that chain-of-
// pointers version was responsible for roughly a third of this viewer's
// per-frame CPU cost, almost all cache misses. Go's built-in map doesn't
// have that failure mode (no per-entry allocation, no pointer-chasing
// buckets), so the native map is the direct equivalent of the *fix*, not
// a step back from it - no custom hash table needed here.
func LoadNames(path string) (map[uint64]NameEntry, error) {
	names := make(map[uint64]NameEntry)

	f, err := os.Open(path)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Warning: could not open %s, city names will be unavailable.\n", path)
		return names, nil
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, "\t", 3)
		if len(parts) != 3 {
			continue
		}
		id, err := strconv.ParseUint(parts[0], 10, 64)
		if err != nil {
			continue
		}
		pop, _ := strconv.ParseInt(parts[2], 10, 64)
		names[id] = NameEntry{Name: parts[1], Population: pop}
	}
	if err := scanner.Err(); err != nil {
		return names, err
	}

	fmt.Printf("Loaded %d city names.\n", len(names))
	return names, nil
}
