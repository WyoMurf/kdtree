package main

import (
	"fmt"

	rl "github.com/gen2brain/raylib-go/raylib"

	"github.com/WyoMurf/kdtree/viewers/go/kdmmap"
)

// Tile is the per-tile mmap cache, same lazy-load-once-and-keep pattern as
// viewer.c's Shard/EnsureShardLoaded (no eviction - at ~390 tiles totaling
// maybe a few MB, keeping them all mapped for the process lifetime once
// touched isn't a concern the way it was for the Gaia viewer's ~33,500
// star shards).
type Tile struct {
	mapped    *kdmmap.MappedNodes2F64
	attempted bool
	positions []rl.Vector3 // cached LonLatToCartesian per node, computed once on load
}

type World struct {
	metaTree      *kdmmap.MappedNodes2F64
	manifestPaths []string
	tiles         []Tile
	tilesLoaded   int
}

func LoadWorld(dir string) (*World, error) {
	metaTree, err := kdmmap.OpenNodes2F64(dir + "/cities.metatree")
	if err != nil {
		return nil, err
	}

	manifestPaths, err := kdmmap.LoadManifest(dir + "/cities.manifest")
	if err != nil {
		metaTree.Close()
		return nil, err
	}
	if len(manifestPaths) != len(metaTree.Nodes) {
		metaTree.Close()
		return nil, fmt.Errorf("cities.manifest has %d entries, expected %d (doesn't match cities.metatree)",
			len(manifestPaths), len(metaTree.Nodes))
	}

	return &World{
		metaTree:      metaTree,
		manifestPaths: manifestPaths,
		tiles:         make([]Tile, len(manifestPaths)),
	}, nil
}

func (w *World) Close() {
	for i := range w.tiles {
		if w.tiles[i].mapped != nil {
			w.tiles[i].mapped.Close()
		}
	}
	w.metaTree.Close()
}

// EnsureTileLoaded lazily mmaps the tile at this manifest index, if it
// hasn't been tried yet, and precomputes every node's Cartesian position
// (fixed city data - never changes frame to frame, so there's no reason to
// redo LonLatToCartesian's trig on it every single frame the way naive
// per-frame projection would).
func (w *World) EnsureTileLoaded(manifestIdx uint64) *Tile {
	if manifestIdx >= uint64(len(w.tiles)) {
		return nil
	}
	tile := &w.tiles[manifestIdx]
	if tile.attempted {
		if tile.mapped != nil {
			return tile
		}
		return nil
	}
	tile.attempted = true

	mapped, err := kdmmap.OpenNodes2F64(w.manifestPaths[manifestIdx])
	if err != nil {
		return nil
	}
	tile.mapped = mapped

	tile.positions = make([]rl.Vector3, len(mapped.Nodes))
	for i, n := range mapped.Nodes {
		lon := (n.Size[0] + n.Size[2]) / 2.0
		lat := (n.Size[1] + n.Size[3]) / 2.0
		tile.positions[i] = LonLatToCartesian(lon, lat, earthRadiusKm)
	}

	w.tilesLoaded++
	return tile
}

// DrawTilePoints draws every point in this tile's already-loaded data that
// passes its own horizon+frustum test, and queues a name label for it if
// close enough - the tile-level CellVisible check only gates whether the
// tile was loaded/scanned at all, so any looseness there never leaks into
// what's actually rendered.
func (r *Renderer) DrawTilePoints(tile *Tile, fr [6]Plane, ht HorizonTest, camPos rl.Vector3, altitudeKm float32, names map[uint64]NameEntry) {
	for i := range tile.mapped.Nodes {
		node := &tile.mapped.Nodes[i]
		if node.SourceID == 0 {
			continue
		}
		pos := tile.positions[i]
		if !IsAboveHorizonFast(pos, ht) {
			continue
		}
		if !PointInFrustum(fr, pos) {
			continue
		}

		ne, haveName := names[node.SourceID]
		population := int64(0)
		if haveName {
			population = ne.Population
		}
		popf := float32(10.0)
		if population > 10 {
			popf = float32(population)
		}
		log10Pop := log10f(popf)
		distKm := rl.Vector3Distance(camPos, pos)
		r.DrawCityPoint(pos, CityMarkerWorldSize(distKm, log10Pop))

		if haveName && len(r.labels) < maxLabelsPerFrame {
			// Steep population curve so labels appear progressively, the
			// way a map app would: tiny villages (~1000 people) only label
			// once you're within ~50km; a capital-sized city labels from
			// several hundred km out.
			labelThresholdKm := 60.0*log10Pop - 130.0
			if altitudeKm < labelThresholdKm {
				r.labels = append(r.labels, PendingLabel{WorldPos: pos, Name: ne.Name})
			}
		}
	}
}

// WalkMetaTree visits every meta-tree node (each holds exactly one tile's
// own bounding box - there's no subtree-aggregated box without a
// kd2lod-style annotation, which this viewer deliberately doesn't build; at
// ~390 entries a full scan every frame is trivial) and draws any tile whose
// box survives CellVisible.
func (w *World) WalkMetaTree(r *Renderer, idx int64, fr [6]Plane, ht HorizonTest, camPos rl.Vector3, altitudeKm float32, names map[uint64]NameEntry) {
	if idx < 0 {
		return
	}
	node := &w.metaTree.Nodes[idx]

	if CellVisible(node.Size[0], node.Size[1], node.Size[2], node.Size[3], fr, ht) {
		manifestIdx := node.SourceID - 1
		if tile := w.EnsureTileLoaded(manifestIdx); tile != nil {
			r.DrawTilePoints(tile, fr, ht, camPos, altitudeKm, names)
		}
	}

	w.WalkMetaTree(r, node.LeftChild, fr, ht, camPos, altitudeKm, names)
	w.WalkMetaTree(r, node.RightChild, fr, ht, camPos, altitudeKm, names)
}
