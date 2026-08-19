// Package kdmmap mmaps the binary .kdtree/.metatree files produced by this
// project's C tools (geonames2kd, build_city_metatree, fits2kd, ...) and
// overlays them with Go structs matching the C layout exactly (see
// C/include/kdtree.h). Every field in these node structs is already a
// naturally 8-byte-aligned type in C declaration order, so C's
// #pragma pack(1) has no actual effect on them: Go's default struct layout
// reproduces the same byte layout with no manual offset math or padding
// tricks needed.
package kdmmap

import (
	"fmt"
	"os"
	"unsafe"

	"golang.org/x/sys/unix"
)

// SourceIDSentinel marks the trailing "bounds" record every serialized
// .kdtree/.metatree file may carry (see kd_serialize in the C library):
// a node whose source_id is UINT64_MAX isn't a real item, just whole-tree
// bounds tacked onto the end. Real node counts always exclude it.
const SourceIDSentinel = ^uint64(0)

// Node2F64 mirrors kd_2d_f64_mmap_node: a node in a 2D, float64-keyed
// kd-tree. Size holds [lonMin, latMin, lonMax, latMax] for city tiles (see
// earth_viewer.c's DrawTilePoints/EnsureTileLoaded).
type Node2F64 struct {
	SourceID   uint64
	Size       [4]float64
	LoMinBound float64
	HiMaxBound float64
	OtherBound float64
	LeftChild  int64
	RightChild int64
}

// MappedNodes2F64 is an mmap'd .kdtree/.metatree file overlaid as
// []Node2F64. Nodes excludes the trailing bounds sentinel, if present.
type MappedNodes2F64 struct {
	data  []byte
	Nodes []Node2F64
}

// OpenNodes2F64 mmaps path read-only and overlays it as Node2F64 records.
// Returns an error for a missing/empty/too-small file rather than panicking,
// since callers (lazy per-tile/per-shard loaders) treat "can't load this
// one" as routine, not fatal.
func OpenNodes2F64(path string) (*MappedNodes2F64, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return nil, err
	}
	size := fi.Size()
	if size == 0 {
		return nil, fmt.Errorf("%s: empty file", path)
	}

	data, err := unix.Mmap(int(f.Fd()), 0, int(size), unix.PROT_READ, unix.MAP_PRIVATE)
	if err != nil {
		return nil, fmt.Errorf("%s: mmap: %w", path, err)
	}

	const nodeSize = unsafe.Sizeof(Node2F64{})
	totalCount := len(data) / int(nodeSize)
	if totalCount == 0 {
		unix.Munmap(data)
		return nil, fmt.Errorf("%s: %d bytes, too short for even one node (likely truncated)", path, size)
	}

	nodes := unsafe.Slice((*Node2F64)(unsafe.Pointer(&data[0])), totalCount)
	realCount := totalCount
	if nodes[totalCount-1].SourceID == SourceIDSentinel {
		realCount = totalCount - 1
	}
	if realCount == 0 {
		unix.Munmap(data)
		return nil, fmt.Errorf("%s: contains only the bounds sentinel, no real nodes", path)
	}

	return &MappedNodes2F64{data: data, Nodes: nodes[:realCount]}, nil
}

// Close unmaps the underlying file. After Close, Nodes must not be accessed.
func (m *MappedNodes2F64) Close() error {
	return unix.Munmap(m.data)
}

// Node3I64 mirrors kd_3d_64_mmap_node: a node in a 3D, int64-keyed kd-tree.
// Size holds [xMin, yMin, zMin, xMax, yMax, zMax] in fixed-point units (see
// fits2kd.c's SCALE_FACTOR) for Gaia star shards and the star meta-tree.
type Node3I64 struct {
	SourceID   uint64
	Size       [6]int64
	LoMinBound int64
	HiMaxBound int64
	OtherBound int64
	LeftChild  int64
	RightChild int64
}

// MappedNodes3I64 is an mmap'd .kdtree/.metatree file overlaid as
// []Node3I64. Nodes excludes the trailing bounds sentinel, if present.
// SourceSize is the raw file size, kept around because a shard's .lod
// sidecar must be validated against it (see OpenLod).
type MappedNodes3I64 struct {
	data       []byte
	Nodes      []Node3I64
	SourceSize uint64
}

// OpenNodes3I64 mmaps path read-only and overlays it as Node3I64 records.
func OpenNodes3I64(path string) (*MappedNodes3I64, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return nil, err
	}
	size := fi.Size()
	if size == 0 {
		return nil, fmt.Errorf("%s: empty file", path)
	}

	data, err := unix.Mmap(int(f.Fd()), 0, int(size), unix.PROT_READ, unix.MAP_PRIVATE)
	if err != nil {
		return nil, fmt.Errorf("%s: mmap: %w", path, err)
	}

	const nodeSize = unsafe.Sizeof(Node3I64{})
	totalCount := len(data) / int(nodeSize)
	if totalCount == 0 {
		unix.Munmap(data)
		return nil, fmt.Errorf("%s: %d bytes, too short for even one node (likely truncated)", path, size)
	}

	nodes := unsafe.Slice((*Node3I64)(unsafe.Pointer(&data[0])), totalCount)
	realCount := totalCount
	if nodes[totalCount-1].SourceID == SourceIDSentinel {
		realCount = totalCount - 1
	}
	if realCount == 0 {
		unix.Munmap(data)
		return nil, fmt.Errorf("%s: contains only the bounds sentinel, no real nodes", path)
	}

	return &MappedNodes3I64{data: data, Nodes: nodes[:realCount], SourceSize: uint64(size)}, nil
}

// Close unmaps the underlying file. After Close, Nodes must not be accessed.
func (m *MappedNodes3I64) Close() error {
	return unix.Munmap(m.data)
}

// LOD sidecar format (produced by kd2lod.c, see C/include/kd2lod.h): record
// i corresponds exactly to node i in the source .kdtree file, so a viewer
// can mmap both and use left_child/right_child from the .kdtree file to
// walk this array directly.
const (
	LodMagic   uint32 = 0x32444F4C // "LOD2" little-endian
	LodVersion uint32 = 1
)

type LodHeader struct {
	Magic      uint32
	Version    uint32
	SourceSize uint64 // st_size of the source .kdtree file at annotation time
	NodeCount  uint64 // number of records that follow (excludes the bounds sentinel)
}

type LodRecord struct {
	Min   [3]int64
	Max   [3]int64
	Count uint32
	_     uint32 // padding, kept for exact layout parity with kd2lod_record
}

// MappedLod is an mmap'd .kdtree.lod/.metatree.lod sidecar file.
type MappedLod struct {
	data    []byte
	Header  LodHeader
	Records []LodRecord
}

// OpenLod mmaps path and validates its header against the source .kdtree/
// .metatree file it must exactly match (same size, same node count) -
// otherwise it's stale (source was rebuilt without re-running kd2lod) and
// must be rejected rather than trusted. Mirrors the validation
// LoadOneShard/LoadMetaTree do in viewer.c.
func OpenLod(path string, expectedSourceSize, expectedNodeCount uint64) (*MappedLod, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return nil, err
	}
	size := fi.Size()
	headerSize := int64(unsafe.Sizeof(LodHeader{}))
	if size < headerSize {
		return nil, fmt.Errorf("%s: %d bytes, too short for a kd2lod header", path, size)
	}

	data, err := unix.Mmap(int(f.Fd()), 0, int(size), unix.PROT_READ, unix.MAP_PRIVATE)
	if err != nil {
		return nil, fmt.Errorf("%s: mmap: %w", path, err)
	}

	hdr := *(*LodHeader)(unsafe.Pointer(&data[0]))
	recordSize := uint64(unsafe.Sizeof(LodRecord{}))
	expectedSize := uint64(headerSize) + hdr.NodeCount*recordSize

	if hdr.Magic != LodMagic || hdr.Version != LodVersion ||
		hdr.SourceSize != expectedSourceSize || hdr.NodeCount != expectedNodeCount ||
		uint64(size) != expectedSize {
		unix.Munmap(data)
		return nil, fmt.Errorf("%s: stale or invalid (doesn't match its source file)", path)
	}

	records := unsafe.Slice((*LodRecord)(unsafe.Pointer(&data[headerSize])), int(hdr.NodeCount))
	return &MappedLod{data: data, Header: hdr, Records: records}, nil
}

// Close unmaps the underlying file. After Close, Records must not be
// accessed.
func (m *MappedLod) Close() error {
	return unix.Munmap(m.data)
}

// LoadManifest reads a newline-delimited list of file paths, in the same
// format LoadManifest()/build_metatree/build_city_metatree write in the C
// tools: line i names the shard/tile file for manifest index i.
func LoadManifest(path string) ([]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var lines []string
	start := 0
	for i, b := range data {
		if b == '\n' {
			line := string(data[start:i])
			line = trimCR(line)
			lines = append(lines, line)
			start = i + 1
		}
	}
	if start < len(data) {
		lines = append(lines, trimCR(string(data[start:])))
	}
	return lines, nil
}

func trimCR(s string) string {
	if len(s) > 0 && s[len(s)-1] == '\r' {
		return s[:len(s)-1]
	}
	return s
}
