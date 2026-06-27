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
}
```

## Testing

Run the standard generic test suite using:
```bash
go test -v ./...
```
