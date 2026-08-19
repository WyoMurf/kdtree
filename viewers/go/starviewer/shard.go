package main

import (
	"fmt"
	"math"

	rl "github.com/gen2brain/raylib-go/raylib"

	"github.com/WyoMurf/kdtree/viewers/go/kdmmap"
)

// Shard is one mmap'd .kdtree shard, plus (if present and valid) the
// mmap'd .kdtree.lod sidecar of per-node subtree bounds/counts produced by
// kd2lod. Shards are mmap'd lazily, the first time the meta-tree walk
// actually visits one - opening/fstat/mmap-ing ~36,900 shard files is
// disk-I/O-bound and was measured to take minutes even at high parallelism,
// long enough for the window manager to call the process "not responding".
type Shard struct {
	mapped    *kdmmap.MappedNodes3I64
	lod       *kdmmap.MappedLod // nil if no valid sidecar was found
	attempted bool              // true once a load has been tried (success or failure)
}

// World is the meta-tree over every shard file's own bounding box (built by
// build_metatree, annotated by kd2lod exactly like any other .kdtree file),
// plus the lazily-loaded shards themselves.
type World struct {
	metaTree      *kdmmap.MappedNodes3I64
	metaLod       *kdmmap.MappedLod
	manifestPaths []string
	shards        []Shard

	shardsLoadedCount   uint64
	starsDiscovered     uint64
	shardLoadsThisFrame int
}

// LoadWorld mmaps catalog.metatree + catalog.metatree.lod, and reads
// catalog.manifest, from the given directory. Unlike a per-shard .lod
// sidecar (optional - see EnsureShardLoaded's brute-force fallback), the
// meta-tree's own .lod is required: without it there's no way to cull
// whole shards by bounding box at all.
func LoadWorld(dir string) (*World, error) {
	metaTree, err := kdmmap.OpenNodes3I64(dir + "/catalog.metatree")
	if err != nil {
		return nil, err
	}

	metaLod, err := kdmmap.OpenLod(dir+"/catalog.metatree.lod", metaTree.SourceSize, uint64(len(metaTree.Nodes)))
	if err != nil {
		metaTree.Close()
		return nil, fmt.Errorf("%w\nRun: kd2lod %s/catalog.metatree %s/catalog.metatree.lod", err, dir, dir)
	}

	manifestPaths, err := kdmmap.LoadManifest(dir + "/catalog.manifest")
	if err != nil {
		metaLod.Close()
		metaTree.Close()
		return nil, err
	}
	if len(manifestPaths) != len(metaTree.Nodes) {
		metaLod.Close()
		metaTree.Close()
		return nil, fmt.Errorf("catalog.manifest has %d entries, expected %d (doesn't match catalog.metatree)",
			len(manifestPaths), len(metaTree.Nodes))
	}

	return &World{
		metaTree:      metaTree,
		metaLod:       metaLod,
		manifestPaths: manifestPaths,
		shards:        make([]Shard, len(manifestPaths)),
	}, nil
}

func (w *World) Close() {
	for i := range w.shards {
		if w.shards[i].lod != nil {
			w.shards[i].lod.Close()
		}
		if w.shards[i].mapped != nil {
			w.shards[i].mapped.Close()
		}
	}
	w.metaLod.Close()
	w.metaTree.Close()
}

// EnsureShardLoaded lazily mmaps the shard at this manifest index, if it
// hasn't been tried yet, subject to the per-frame load cap. Returns nil if
// the shard isn't loaded (whether because loading failed, or because the
// attempt is deferred to a later frame to stay within the cap) - callers
// treat nil exactly like "nothing to draw here yet".
func (w *World) EnsureShardLoaded(manifestIdx uint64) *Shard {
	if manifestIdx >= uint64(len(w.shards)) {
		return nil
	}
	shard := &w.shards[manifestIdx]
	if shard.attempted {
		if shard.mapped != nil {
			return shard
		}
		return nil
	}
	if w.shardLoadsThisFrame >= maxShardLoadsPerFrame {
		return nil // retry next frame
	}
	shard.attempted = true
	w.shardLoadsThisFrame++

	path := w.manifestPaths[manifestIdx]
	mapped, err := kdmmap.OpenNodes3I64(path)
	if err != nil {
		return nil
	}
	shard.mapped = mapped

	// Look for a sidecar .kdtree.lod file annotated by kd2lod. Unlike the
	// meta-tree's own .lod, a missing/stale one here just means this shard
	// falls back to brute-force rendering (DrawShardBruteForce), not a
	// fatal error.
	if lod, err := kdmmap.OpenLod(path+".lod", mapped.SourceSize, uint64(len(mapped.Nodes))); err == nil {
		shard.lod = lod
	}

	w.shardsLoadedCount++
	if shard.lod != nil {
		w.starsDiscovered += uint64(shard.lod.Records[0].Count)
	} else {
		w.starsDiscovered += uint64(len(mapped.Nodes))
	}
	return shard
}

func boxFromLod(rec *kdmmap.LodRecord) (bmin, bmax rl.Vector3) {
	bmin = rl.NewVector3(
		float32(float64(rec.Min[0])/scaleFactor),
		float32(float64(rec.Min[1])/scaleFactor),
		float32(float64(rec.Min[2])/scaleFactor),
	)
	bmax = rl.NewVector3(
		float32(float64(rec.Max[0])/scaleFactor),
		float32(float64(rec.Max[1])/scaleFactor),
		float32(float64(rec.Max[2])/scaleFactor),
	)
	return
}

func angularSizeOf(bmin, bmax rl.Vector3, camPos rl.Vector3) float32 {
	ext := rl.NewVector3(bmax.X-bmin.X, bmax.Y-bmin.Y, bmax.Z-bmin.Z)
	diag := float32(math.Sqrt(float64(ext.X*ext.X + ext.Y*ext.Y + ext.Z*ext.Z)))
	center := rl.NewVector3((bmin.X+bmax.X)*0.5, (bmin.Y+bmax.Y)*0.5, (bmin.Z+bmax.Z)*0.5)
	centerDist := rl.Vector3Distance(camPos, center)
	if centerDist < 0.001 {
		centerDist = 0.001
	}
	return diag / centerDist
}

// CullAndCollect is the core LOD walk within a single shard. Every node
// holds a real star, so "collapsing" just means: draw this node's own star
// (boosted to represent its whole subtree) and stop, instead of recursing
// into children to draw them individually.
func CullAndCollect(r *Renderer, shard *Shard, idx int64, fr [6]Plane, camPos rl.Vector3, angleThreshold float32) {
	if idx < 0 {
		return
	}
	if r.pointsDrawn >= framePointBudget {
		r.budgetHit = true
		return
	}

	rec := &shard.lod.Records[idx]
	bmin, bmax := boxFromLod(rec)
	if AABBOutsideFrustum(fr, bmin, bmax) {
		return
	}
	angularSize := angularSizeOf(bmin, bmax, camPos)

	node := &shard.mapped.Nodes[idx]
	starPos := NodeStarPos(node)
	starDist := rl.Vector3Distance(camPos, starPos)

	if angularSize < angleThreshold {
		r.DrawStarPoint(starPos, starDist, rec.Count)
		r.nodesCollapsed++
		return
	}

	r.DrawStarPoint(starPos, starDist, 1)
	r.nodesExpanded++

	CullAndCollect(r, shard, node.LeftChild, fr, camPos, angleThreshold)
	CullAndCollect(r, shard, node.RightChild, fr, camPos, angleThreshold)
}

// DrawShardBruteForce is the fallback for shards with no (or a stale/
// mismatched) .lod sidecar: draw every star. Correct but slow - no
// frustum culling; run kd2lod on the shard's .kdtree file to speed it up.
func DrawShardBruteForce(r *Renderer, shard *Shard, camPos rl.Vector3) {
	for i := range shard.mapped.Nodes {
		if r.pointsDrawn >= framePointBudget {
			r.budgetHit = true
			return
		}
		node := &shard.mapped.Nodes[i]
		if node.SourceID == 0 {
			continue
		}
		starPos := NodeStarPos(node)
		dist := rl.Vector3Distance(camPos, starPos)
		r.DrawStarPoint(starPos, dist, 1)
	}
}

// CullAndCollectMeta walks the meta-tree exactly like CullAndCollect walks
// a shard's star-tree, one level up: a meta-tree node's own data is a
// shard's bounding box rather than a star's position. "Expanding" a node
// lazily loads that shard (if not already cached) and walks its real stars
// via CullAndCollect; "collapsing" draws one representative point standing
// in for every shard hidden in that subtree. Note rec.Count here is shards
// hidden, not stars hidden (kd2lod counts tree nodes generically); treating
// it as a brightness proxy is an approximation, since finding the true
// star count would mean opening every shard, defeating the point of
// staying lazy.
func (w *World) CullAndCollectMeta(r *Renderer, idx int64, fr [6]Plane, camPos rl.Vector3, angleThreshold float32) {
	if idx < 0 {
		return
	}
	if r.pointsDrawn >= framePointBudget {
		r.budgetHit = true
		return
	}

	rec := &w.metaLod.Records[idx]
	bmin, bmax := boxFromLod(rec)
	if AABBOutsideFrustum(fr, bmin, bmax) {
		return
	}
	angularSize := angularSizeOf(bmin, bmax, camPos)

	metaNode := &w.metaTree.Nodes[idx]

	if angularSize < angleThreshold {
		shardCenter := NodeStarPos(metaNode)
		dist := rl.Vector3Distance(camPos, shardCenter)
		r.DrawStarPoint(shardCenter, dist, rec.Count)
		r.nodesCollapsed++
		return
	}

	manifestIdx := metaNode.SourceID - 1
	if shard := w.EnsureShardLoaded(manifestIdx); shard != nil {
		if shard.lod != nil {
			CullAndCollect(r, shard, 0, fr, camPos, angleThreshold)
		} else {
			DrawShardBruteForce(r, shard, camPos)
		}
	}
	r.nodesExpanded++

	w.CullAndCollectMeta(r, metaNode.LeftChild, fr, camPos, angleThreshold)
	w.CullAndCollectMeta(r, metaNode.RightChild, fr, camPos, angleThreshold)
}
