# 3D KD-Tree (Go Implementation)

This directory contains the unified Go implementation of a 3D KD-Tree, parameterized using Go Generics to support integer coordinate widths.

## Compilation & Usage

This library requires Go 1.18+ to support Generic type constraints.

You do not need to configure anything during the build process. Function names remain clean and idiomatic, without any bit-size suffixes. You define the bit-width when you instantiate the generic types.

```go
import "github.com/WyoMurf/kdtree/3d/go"

func main() {
    // Instantiate a 32-bit KDTree
    tree32 := kd.Create[int32]()
    box32 := kd.Box[int32]{0, 0, 0, 10, 10, 10}
    tree32.Insert("item32", box32)

    // Instantiate a 64-bit KDTree
    tree64 := kd.Create[int64]()
    box64 := kd.Box[int64]{0, 0, 0, 10, 10, 10}
    tree64.Insert("item64", box64)

    // Instantiate a float64 KDTree, for real-valued coordinates
    treef64 := kd.Create[float64]()
    boxf64 := kd.Box[float64]{0, 0, 0, 10.5, 10.5, 10.5}
    treef64.Insert("itemf64", boxf64)
}
```

## Geo & Angle Utilities

Also exported, unrelated to the tree itself: great-circle distance calculators and DMS↔degrees conversion.

```go
// Fast, approximate (perfect sphere)
km := kd.HaversineDistance(40.7128, -74.0060, 51.5074, -0.1278, kd.EarthRadiusKm)

// Slow, exact (oblate spheroid) -- Earth's WGS-84 constants are provided for you
m := kd.VincentyDistance(40.7128, -74.0060, 51.5074, -0.1278, kd.EarthSemiMajorAxisM, kd.EarthFlattening)

// DMS <-> decimal degrees (sign carried separately so -0deg 15min is representable)
degrees := kd.DmsToDegrees[int32](-1, 73, 58, 34.0)
sign, deg, min, sec := kd.DegreesToDms[int32](degrees)
```

## Testing

Run the standard generic test suite using:
```bash
go test -v ./...
```
