package kd

import (
	"math"
	"math/rand"
	"os"
	"sort"
	"testing"
)

const (
	KD_BOXES     = 10000
	KD_REGIONS   = 100
	MIN_RANGE    = -20000
	MAX_RANGE    = 20000
	RANGE_SPAN   = MAX_RANGE - MIN_RANGE + 1
	BOX_RANGE    = 1000
)

func randBox(rng *rand.Rand) Box[int32] {
	var b Box[int32]
	b[Left] = int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)
	b[Bottom] = int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)
	b[Floor] = int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)
	b[Right] = b[Left] + int32(rng.Intn(BOX_RANGE))
	b[Top] = b[Bottom] + int32(rng.Intn(BOX_RANGE))
	b[Ceil] = b[Floor] + int32(rng.Intn(BOX_RANGE))
	return b
}

func TestKD3D(t *testing.T) {
	tree := Create[int32]()
	
	box1 := Box[int32]{0, 0, 0, 10, 10, 10}
	box2 := Box[int32]{20, 20, 20, 30, 30, 30}
	box3 := Box[int32]{5, 5, 5, 15, 15, 15}

	tree.Insert("item1", box1)
	tree.Insert("item2", box2)
	tree.Insert("item3", box3)

	if tree.Count() != 3 {
		t.Errorf("Expected count 3, got %d", tree.Count())
	}

	if !tree.IsMember("item2", box2) {
		t.Error("item2 not found in tree")
	}

	// Range search
	searchArea := Box[int32]{0, 0, 0, 12, 12, 12}
	gen := tree.Start(searchArea)
	found := 0
	for {
		item, _, ok := gen.Next()
		if !ok {
			break
		}
		if item == "item1" || item == "item3" {
			found++
		}
	}
	if found != 2 {
		t.Errorf("Expected 2 items in range search, got %d", found)
	}
}

func TestSoftDelete(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box[int32], KD_BOXES)
	tree := Create[int32]()

	for i := 0; i < KD_BOXES; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	// Search verification
	for i := 0; i < KD_REGIONS; i++ {
		region := randBox(rng)
		gen := tree.Start(region)
		foundItems := make(map[int]bool)
		for {
			item, _, ok := gen.Next()
			if !ok {
				break
			}
			foundItems[item.(int)] = true
		}

		for j, box := range boxes {
			intersect := Intersect(region, box)
			if intersect {
				if !foundItems[j+1] {
					t.Fatalf("[soft] Missing item %d in search", j+1)
				}
				delete(foundItems, j+1)
			}
		}
		if len(foundItems) > 0 {
			t.Fatalf("[soft] Extra items in search: %v", foundItems)
		}
	}

	// Soft delete everything
	for i := 0; i < KD_BOXES; i++ {
		if !tree.Delete(i+1, boxes[i]) {
			t.Fatalf("[soft] Failed to delete item %d", i+1)
		}
	}

	if tree.Count() != 0 {
		t.Errorf("[soft] Expected count 0, got %d", tree.Count())
	}

	// Verify empty
	gen := tree.Start(Box[int32]{MIN_RANGE - 1, MIN_RANGE - 1, MIN_RANGE - 1, MAX_RANGE + 1, MAX_RANGE + 1, MAX_RANGE + 1})
	if item, _, ok := gen.Next(); ok {
		t.Errorf("[soft] Tree not empty, found %v", item)
	}
}

func TestHardDelete(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box[int32], KD_BOXES)
	tree := Create[int32]()

	for i := 0; i < KD_BOXES; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	if tree.Count() != KD_BOXES {
		t.Fatalf("[hard] Expected initial count %d, got %d", KD_BOXES, tree.Count())
	}

	// Hard delete everything in reverse order
	for i := KD_BOXES - 1; i >= 0; i-- {
		deleted := tree.HardDelete(i+1, boxes[i])
		if !deleted {
			if !tree.IsMember(i+1, boxes[i]) {
				t.Fatalf("[hard] Item %d not found in tree before delete", i+1)
			}
			t.Fatalf("[hard] Failed to hard delete item %d", i+1)
		}
	}

	if tree.Count() != 0 {
		t.Errorf("[hard] Expected count 0, got %d", tree.Count())
	}

	// Verify empty
	gen := tree.Start(Box[int32]{MIN_RANGE - 1, MIN_RANGE - 1, MIN_RANGE - 1, MAX_RANGE + 1, MAX_RANGE + 1, MAX_RANGE + 1})
	if item, _, ok := gen.Next(); ok {
		t.Errorf("[hard] Tree not empty, found %v", item)
	}
}

func TestNearest(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box[int32], KD_BOXES)
	tree := Create[int32]()

	for i := 0; i < KD_BOXES; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	for m := 1; m <= 20; m *= 2 {
		for q := 0; q < 50; q++ { // 50 queries instead of 100 for speed
			qx := int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)
			qy := int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)
			qz := int32(rng.Intn(RANGE_SPAN) + MIN_RANGE)

			list := tree.Nearest(qx, qy, qz, m)
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
			bruteDists := make([]float64, KD_BOXES)
			qBox := Box[int32]{qx, qy, qz, qx, qy, qz}
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
	qz := (boxes[0][Floor] + boxes[0][Ceil]) / 2
	list := tree.Nearest(qx, qy, qz, 1)
	if list[0].Dist > 1e-9 {
		t.Errorf("[nearest] Edge case fail: dist=%g", list[0].Dist)
	}
}

func TestReallyDelete(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	boxes := make([]Box[int32], KD_BOXES)
	tree := Create[int32]()

	for i := 0; i < KD_BOXES; i++ {
		boxes[i] = randBox(rng)
		tree.Insert(i+1, boxes[i])
	}

	if tree.Count() != KD_BOXES {
		t.Fatalf("[really] Expected initial count %d, got %d", KD_BOXES, tree.Count())
	}

	// Really delete everything in reverse order
	for i := KD_BOXES - 1; i >= 0; i-- {
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
	gen := tree.Start(Box[int32]{MIN_RANGE - 1, MIN_RANGE - 1, MIN_RANGE - 1, MAX_RANGE + 1, MAX_RANGE + 1, MAX_RANGE + 1})
	if item, _, ok := gen.Next(); ok {
		t.Errorf("[really] Tree not empty, found %v", item)
	}
}

func TestBadness(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	tree := Create[int32]()

	for i := 0; i < 1000; i++ {
		b := randBox(rng)
		tree.Insert(i+1, b)
	}

	tree.Badness() // Should run and print balance stats without errors
}

func TestSerialize(t *testing.T) {
	tree := Create[int32]()

	tree.Insert("item1", Box[int32]{10, 10, 10, 10, 10, 10})
	tree.Insert("item2", Box[int32]{20, 20, 20, 20, 20, 20})
	tree.Insert("item3", Box[int32]{5, 5, 5, 5, 5, 5})

	// Test GetBounds
	bounds, err := tree.GetBounds()
	if err != nil {
		t.Fatalf("GetBounds failed: %v", err)
	}
	expectedBounds := Box[int32]{5, 5, 5, 20, 20, 20}
	if bounds != expectedBounds {
		t.Fatalf("Expected bounds %v, got %v", expectedBounds, bounds)
	}

	err = tree.Serialize("test_serialize.kdtree", func(item interface{}) uint64 {
		switch item.(string) {
		case "item1": return 1
		case "item2": return 2
		case "item3": return 3
		}
		return 0
	})
	if err != nil {
		t.Fatalf("Serialize failed: %v", err)
	}

	info, err := os.Stat("test_serialize.kdtree")
	if err != nil {
		t.Fatalf("Stat failed: %v", err)
	}

	expectedSize := int64(3 * 60)
	if info.Size() != expectedSize {
		t.Fatalf("Expected size %d, got %d", expectedSize, info.Size())
	}

	// Test GetSerializedBounds
	serBounds, err := GetSerializedBounds[int32]("test_serialize.kdtree")
	if err != nil {
		t.Fatalf("GetSerializedBounds failed: %v", err)
	}
	if serBounds != expectedBounds {
		t.Fatalf("Expected serialized bounds %v, got %v", expectedBounds, serBounds)
	}

	os.Remove("test_serialize.kdtree")
}

func TestFloat64Basic(t *testing.T) {
	tree := Create[float64]()

	box1 := Box[float64]{0.0, 0.0, 0.0, 10.5, 10.5, 10.5}
	box2 := Box[float64]{20.25, 20.25, 20.25, 30.75, 30.75, 30.75}
	box3 := Box[float64]{5.5, 5.5, 5.5, 15.5, 15.5, 15.5}

	tree.Insert("item1", box1)
	tree.Insert("item2", box2)
	tree.Insert("item3", box3)

	if tree.Count() != 3 {
		t.Errorf("Expected count 3, got %d", tree.Count())
	}

	if !tree.IsMember("item2", box2) {
		t.Error("item2 not found in tree")
	}

	searchArea := Box[float64]{0.0, 0.0, 0.0, 12.0, 12.0, 12.0}
	gen := tree.Start(searchArea)
	found := 0
	for {
		item, _, ok := gen.Next()
		if !ok {
			break
		}
		if item == "item1" || item == "item3" {
			found++
		}
	}
	if found != 2 {
		t.Errorf("Expected 2 items in range search, got %d", found)
	}

	tree.Delete("item1", box1)
	if tree.Count() != 2 {
		t.Errorf("Expected count 2 after delete, got %d", tree.Count())
	}

	// Nearest neighbor with fractional coordinates
	list := tree.Nearest(20.25, 20.25, 20.25, 1)
	if len(list) != 1 || list[0].Item != "item2" {
		t.Fatalf("Expected item2 as nearest, got %v", list)
	}
	if list[0].Dist > 1e-9 {
		t.Errorf("Expected dist ~0, got %g", list[0].Dist)
	}
}

func TestSerializeFloat64(t *testing.T) {
	tree := Create[float64]()

	tree.Insert("item1", Box[float64]{1.5, 1.5, 1.5, 1.5, 1.5, 1.5})
	tree.Insert("item2", Box[float64]{-3.25, -3.25, -3.25, -3.25, -3.25, -3.25})
	tree.Insert("item3", Box[float64]{100.125, 100.125, 100.125, 100.125, 100.125, 100.125})

	bounds, err := tree.GetBounds()
	if err != nil {
		t.Fatalf("GetBounds failed: %v", err)
	}
	expectedBounds := Box[float64]{-3.25, -3.25, -3.25, 100.125, 100.125, 100.125}
	if bounds != expectedBounds {
		t.Fatalf("Expected bounds %v, got %v", expectedBounds, bounds)
	}

	err = tree.Serialize("test_serialize_float64.kdtree", func(item interface{}) uint64 {
		switch item.(string) {
		case "item1": return 1
		case "item2": return 2
		case "item3": return 3
		}
		return 0
	})
	if err != nil {
		t.Fatalf("Serialize failed: %v", err)
	}
	defer os.Remove("test_serialize_float64.kdtree")

	// Test GetSerializedBounds - this exercises the float64 decode path,
	// which must reinterpret raw IEEE-754 bits rather than numerically
	// casting from an int32/int64 value.
	serBounds, err := GetSerializedBounds[float64]("test_serialize_float64.kdtree")
	if err != nil {
		t.Fatalf("GetSerializedBounds failed: %v", err)
	}
	if serBounds != expectedBounds {
		t.Fatalf("Expected serialized bounds %v, got %v", expectedBounds, serBounds)
	}

	// Confirm exact round-trip of fractional values through serialization.
	if serBounds[Left] != -3.25 {
		t.Errorf("Expected exact round-trip of -3.25, got %v", serBounds[Left])
	}
	if serBounds[Right] != 100.125 {
		t.Errorf("Expected exact round-trip of 100.125, got %v", serBounds[Right])
	}
}

// testDmsRoundTrip runs the DegreesToDms -> DmsToDegrees round trip for
// coordinate type T over a representative set of angles.
func testDmsRoundTrip[T Coord](t *testing.T, angles []float64) {
	for _, x := range angles {
		sign, deg, min, sec := DegreesToDms[T](x)
		got := DmsToDegrees[T](sign, deg, min, sec)
		if math.Abs(got-x) > 1e-9 {
			t.Errorf("round trip failed for %v: DegreesToDms->DmsToDegrees gave %v (sign=%d deg=%v min=%v sec=%v)",
				x, got, sign, deg, min, sec)
		}
	}
}

func TestDmsRoundTrip(t *testing.T) {
	angles := []float64{0.0, 0.25, -0.25, 45.5, -45.5, 89.9999, -89.9999, 123.456789, -123.456789, 180.0, -180.0, 90.0, -90.0}

	t.Run("int32", func(t *testing.T) { testDmsRoundTrip[int32](t, angles) })
	t.Run("int64", func(t *testing.T) { testDmsRoundTrip[int64](t, angles) })
	t.Run("int", func(t *testing.T) { testDmsRoundTrip[int](t, angles) })
	t.Run("float64", func(t *testing.T) { testDmsRoundTrip[float64](t, angles) })
}

func TestDmsNegativeNearZero(t *testing.T) {
	// sign=-1, deg=0, min=15, sec=0.0 should decode to exactly -0.25 degrees.
	got := DmsToDegrees[int](-1, 0, 15, 0.0)
	if got != -0.25 {
		t.Errorf("expected -0.25, got %v", got)
	}

	// And the reverse: -0.25 degrees should decode to sign=-1, deg=0, min=15, sec=0.0.
	sign, deg, min, sec := DegreesToDms[int](-0.25)
	if sign != -1 || deg != 0 || min != 15 || sec != 0.0 {
		t.Errorf("expected sign=-1 deg=0 min=15 sec=0.0, got sign=%d deg=%v min=%v sec=%v", sign, deg, min, sec)
	}
}

func TestHaversineDistanceQuarterGreatCircle(t *testing.T) {
	radius := EarthRadiusKm
	got := HaversineDistance(0.0, 0.0, 90.0, 0.0, radius)
	want := radius * math.Pi / 2.0
	if math.Abs(got-want) > 1e-9 {
		t.Errorf("expected %v, got %v", want, got)
	}
}

func TestVincentyDistanceAlongEquator(t *testing.T) {
	delta := 10.0
	got := VincentyDistance(0.0, 0.0, 0.0, delta, EarthSemiMajorAxisM, EarthFlattening)
	want := EarthSemiMajorAxisM * delta * (math.Pi / 180.0)
	if math.Abs(got-want) > 1e-6 {
		t.Errorf("expected %v, got %v", want, got)
	}
}

func TestVincentyDistanceNearAntipodal(t *testing.T) {
	got := VincentyDistance(0.0, 0.0, 0.001, 179.999, EarthSemiMajorAxisM, EarthFlattening)
	if math.IsNaN(got) || math.IsInf(got, 0) {
		t.Errorf("expected a finite value, got %v", got)
	}
}

// Expected values cross-checked against C/healpix_calc.c (via the shared
// geo_utils.c implementation it now wraps), which this port mirrors exactly.
func TestHealpixNestedIndexMatchesCReference(t *testing.T) {
	cases := []struct {
		raOrLon, decOrLat float64
		level             int
		want              uint64
		note              string
	}{
		{217.4290, -62.6795, 12, 134053741, "polar cap"},
		{-109.05653, 44.52634, 3, 330, "polar cap, negative lon"},
		{45.0, 10.0, 3, 282, "equatorial belt"},
		{0.0, 0.0, 3, 256, "equatorial belt, origin"},
		{200.0, -20.0, 5, 4257, "equatorial belt, higher level"},
	}
	for _, c := range cases {
		got := HealpixNestedIndex(c.raOrLon, c.decOrLat, c.level)
		if got != c.want {
			t.Errorf("%s: HealpixNestedIndex(%v, %v, %d) = %d, want %d", c.note, c.raOrLon, c.decOrLat, c.level, got, c.want)
		}
	}
}

func TestHealpixNestedIndexLongitudeNormalization(t *testing.T) {
	got1 := HealpixNestedIndex(-109.05653, 44.52634, 3)
	got2 := HealpixNestedIndex(250.94347, 44.52634, 3)
	if got1 != got2 {
		t.Errorf("expected -109.05653 and its +360 equivalent to land in the same cell, got %d and %d", got1, got2)
	}
}
