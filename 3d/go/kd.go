package kd

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"
)

type Coord interface {
	~int | ~int32 | ~int64 | ~float64
}

// Earth-related constants for use with HaversineDistance and VincentyDistance.
const (
	// EarthRadiusKm is the mean radius of the Earth in kilometers, suitable
	// for use with HaversineDistance when treating the Earth as a perfect sphere.
	EarthRadiusKm = 6371.0

	// EarthSemiMajorAxisM is the WGS-84 semi-major axis of the Earth in meters,
	// for use with VincentyDistance.
	EarthSemiMajorAxisM = 6378137.0

	// EarthFlattening is the WGS-84 flattening of the Earth, for use with
	// VincentyDistance.
	EarthFlattening = 1.0 / 298.257223563
)

// HaversineDistance computes the great-circle distance between two points
// given in decimal degrees, on a perfect sphere of the given radius. It is
// fast and approximate: it ignores the Earth's (or any body's) oblateness.
// The returned distance is in the same units as radius.
func HaversineDistance(lat1, lon1, lat2, lon2, radius float64) float64 {
	toRad := math.Pi / 180.0
	phi1 := lat1 * toRad
	phi2 := lat2 * toRad
	dphi := (lat2 - lat1) * toRad
	dlambda := (lon2 - lon1) * toRad
	a := math.Pow(math.Sin(dphi/2.0), 2) + math.Cos(phi1)*math.Cos(phi2)*math.Pow(math.Sin(dlambda/2.0), 2)
	c := 2.0 * math.Atan2(math.Sqrt(a), math.Sqrt(1.0-a))
	return radius * c
}

// VincentyDistance computes the geodesic distance between two points given
// in decimal degrees, on an oblate spheroid described by semiMajorAxis and
// flattening. It is slower than HaversineDistance but exact for the modeled
// spheroid. Pass EarthSemiMajorAxisM and EarthFlattening to model the Earth
// specifically using the WGS-84 reference ellipsoid.
//
// The underlying iterative algorithm (Vincenty's formula) is known to fail
// to fully converge for nearly-antipodal points. An iteration cap guards
// against this: rather than looping forever, VincentyDistance returns its
// best-effort estimate after the cap is reached, so the result is always a
// finite value, never a hang or a panic.
func VincentyDistance(lat1, lon1, lat2, lon2, semiMajorAxis, flattening float64) float64 {
	toRad := math.Pi / 180.0
	a := semiMajorAxis
	f := flattening
	b := a * (1.0 - f)
	lat1r := lat1 * toRad
	lat2r := lat2 * toRad
	l := (lon2 - lon1) * toRad
	u1 := math.Atan((1.0 - f) * math.Tan(lat1r))
	u2 := math.Atan((1.0 - f) * math.Tan(lat2r))
	sinU1, cosU1 := math.Sin(u1), math.Cos(u1)
	sinU2, cosU2 := math.Sin(u2), math.Cos(u2)

	lambda := l
	var sinSigma, cosSigma, sigma, cosSqAlpha, cos2SigmaM float64

	for i := 0; i < 200; i++ {
		sinLambda, cosLambda := math.Sin(lambda), math.Cos(lambda)
		sinSigma = math.Sqrt(math.Pow(cosU2*sinLambda, 2) + math.Pow(cosU1*sinU2-sinU1*cosU2*cosLambda, 2))
		if sinSigma == 0 {
			return 0.0 // coincident points
		}
		cosSigma = sinU1*sinU2 + cosU1*cosU2*cosLambda
		sigma = math.Atan2(sinSigma, cosSigma)
		sinAlpha := cosU1 * cosU2 * sinLambda / sinSigma
		cosSqAlpha = 1.0 - sinAlpha*sinAlpha
		if cosSqAlpha != 0.0 {
			cos2SigmaM = cosSigma - 2.0*sinU1*sinU2/cosSqAlpha
		} else {
			cos2SigmaM = 0.0
		}
		c := f / 16.0 * cosSqAlpha * (4.0 + f*(4.0-3.0*cosSqAlpha))
		lambdaPrev := lambda
		lambda = l + (1.0-c)*f*sinAlpha*(sigma+c*sinSigma*(cos2SigmaM+c*cosSigma*(-1.0+2.0*cos2SigmaM*cos2SigmaM)))
		if math.Abs(lambda-lambdaPrev) < 1e-12 {
			break
		}
	}
	// After the loop (converged or hit the 200-iteration cap -- either way, compute
	// and return the best estimate available; do not panic).
	uSq := cosSqAlpha * (a*a - b*b) / (b * b)
	bigA := 1.0 + uSq/16384.0*(4096.0+uSq*(-768.0+uSq*(320.0-175.0*uSq)))
	bigB := uSq / 1024.0 * (256.0 + uSq*(-128.0+uSq*(74.0-47.0*uSq)))
	deltaSigma := bigB * sinSigma * (cos2SigmaM + bigB/4.0*(cosSigma*(-1.0+2.0*cos2SigmaM*cos2SigmaM)-bigB/6.0*cos2SigmaM*(-3.0+4.0*sinSigma*sinSigma)*(-3.0+4.0*cos2SigmaM*cos2SigmaM)))
	return b * bigA * (sigma - deltaSigma)
}

// DmsToDegrees converts a degrees/minutes/seconds angle to decimal degrees.
// deg and min are always non-negative magnitudes; sign (+1 or -1) carries
// the sign of the angle separately, which correctly represents angles
// between -1 and 0 degrees (e.g. -0 deg 15 min) that signed-degrees-only
// representations cannot.
func DmsToDegrees[T Coord](sign int, deg, min T, sec float64) float64 {
	return float64(sign) * (float64(deg) + float64(min)/60.0 + sec/3600.0)
}

// DegreesToDms converts decimal degrees to a degrees/minutes/seconds angle.
// The returned deg and min are always non-negative magnitudes; sign (+1 or
// -1) carries the sign of the angle separately, which correctly represents
// angles between -1 and 0 degrees (e.g. -0 deg 15 min) that signed-degrees-
// only representations cannot.
func DegreesToDms[T Coord](degrees float64) (sign int, deg, min T, sec float64) {
	if degrees < 0 {
		sign = -1
	} else {
		sign = 1
	}
	a := math.Abs(degrees)
	degF := math.Floor(a)
	remMin := (a - degF) * 60.0
	minF := math.Floor(remMin)
	sec = (remMin - minF) * 60.0
	return sign, T(degF), T(minF), sec
}

// Box defines a 3D bounding box [left, bottom, floor, right, top, ceil]
type Box[T Coord] [6]T

const (
	Left   = 0
	Bottom = 1
	Floor  = 2
	Right  = 3
	Top    = 4
	Ceil   = 5
)

// Status represents return values for operations
type Status int

const (
	OK       Status = 1
	NoMore   Status = 2
	NotImpl  Status = -3
	NotFound Status = -4
)

// Node represents an element in the KD-tree
type Node[T Coord] struct {
	Item         interface{}
	Size         Box[T]
	LoMinBound   T
	HiMaxBound   T
	OtherBound   T
	Sons         [2]*Node[T]
}

// Tree represents the KD-tree structure
type Tree[T Coord] struct {
	Root           *Node[T]
	ItemCount      int
	DeadCount      int
	Extent         Box[T]
	ItemsBalanced  int
}

// Create returns a new empty KD-tree
func Create[T Coord]() *Tree[T] {
	return &Tree[T]{}
}

// Disc returns the discriminator for a given level
func Disc(level int) int {
	return level % 6
}

func nextDisc(disc int) int {
	return (disc + 1) % 6
}

func (t *Tree[T]) Insert(data interface{}, size Box[T]) {
	if data == nil {
		panic("KD: attempt to insert nil data")
	}

	if t.Root == nil {
		t.Root = &Node[T]{
			Item:       data,
			Size:       size,
			LoMinBound: size[0],
			HiMaxBound: size[3],
			OtherBound: size[0],
		}
		t.Extent = size
		t.ItemCount = 1
		return
	}

	if t.findItem(t.Root, 0, data, size, false) != nil {
		t.ItemCount++
		// Update tree extent
		for i := 0; i < 3; i++ {
			if size[i] < t.Extent[i] {
				t.Extent[i] = size[i]
			}
			if size[i+3] > t.Extent[i+3] {
				t.Extent[i+3] = size[i+3]
			}
		}
	}
}

func (t *Tree[T]) findItem(elem *Node[T], disc int, item interface{}, size Box[T], searchP bool) *Node[T] {
	if elem == nil {
		return nil
	}
	if item == elem.Item {
		if searchP {
			return elem
		}
		return nil // Duplicate not allowed for insert
	}

	val := size[disc] - elem.Size[disc]

	if searchP && val == 0 {
		// Exact tie on this node's split axis: the item may legitimately live in
		// either subtree. We can't resolve this the way insertion does below
		// (comparing this node's *other* axes), because kdDoDelete's promotion
		// step can later swap a different item into this exact tree position,
		// changing those other-axis values without changing which subtree the
		// original item was placed in -- that would silently misroute this
		// search. Try both sides instead of guessing.
		if res := t.findItem(elem.Sons[0], nextDisc(disc), item, size, searchP); res != nil {
			return res
		}
		return t.findItem(elem.Sons[1], nextDisc(disc), item, size, searchP)
	}

	if val == 0 {
		// Cyclical comparison required (insert path only, at this point)
		ndisc := nextDisc(disc)
		for ndisc != disc {
			val = size[ndisc] - elem.Size[ndisc]
			if val != 0 {
				break
			}
			ndisc = nextDisc(ndisc)
		}
		if val == 0 {
			val = 1
		}
	}

	childIdx := 0
	if val >= 0 {
		childIdx = 1
	}

	if elem.Sons[childIdx] != nil {
		res := t.findItem(elem.Sons[childIdx], nextDisc(disc), item, size, searchP)
		if !searchP && res != nil {
			t.boundsUpdate(elem, disc, size)
		}
		return res
	}

	if searchP {
		return nil
	}

	// Insert here
	vert := int(nextDisc(disc) % 3)
	newNode := &Node[T]{
		Item:       item,
		Size:       size,
		LoMinBound: size[vert],
		HiMaxBound: size[vert+3],
	}
	
	// items_elem->other_bound = ((NEXTDISC(disc)>=3) ? size[vert] : size[vert+3]);
	if nextDisc(disc) >= 3 {
		newNode.OtherBound = size[vert]
	} else {
		newNode.OtherBound = size[vert+3]
	}

	elem.Sons[childIdx] = newNode
	t.boundsUpdate(elem, disc, size)
	return newNode
}

func (t *Tree[T]) boundsUpdate(elem *Node[T], disc int, size Box[T]) {
	vert := int(disc % 3)
	if size[vert] < elem.LoMinBound {
		elem.LoMinBound = size[vert]
	}
	if size[vert+3] > elem.HiMaxBound {
		elem.HiMaxBound = size[vert+3]
	}
	if disc >= 3 {
		if size[vert] < elem.OtherBound {
			elem.OtherBound = size[vert]
		}
	} else {
		if size[vert+3] > elem.OtherBound {
			elem.OtherBound = size[vert+3]
		}
	}
}

type State int

const (
	ThisOne State = 0
	LoSon   State = 1
	HiSon   State = 2
	Done    State = 3
)

type save[T Coord] struct {
	node  *Node[T]
	disc  int
	state State
}

type Generator[T Coord] struct {
	extent Box[T]
	stack  []save[T]
}

func (t *Tree[T]) Start(area Box[T]) *Generator[T] {
	g := &Generator[T]{
		extent: area,
	}
	if t.Root != nil {
		g.stack = append(g.stack, save[T]{node: t.Root, disc: 0, state: ThisOne})
	}
	return g
}

func (g *Generator[T]) Next() (interface{}, Box[T], bool) {
	for len(g.stack) > 0 {
		topIdx := len(g.stack) - 1
		top := &g.stack[topIdx]
		node := top.node
		m := top.disc
		hort := m % 3

		switch top.state {
		case ThisOne:
			g.stack[topIdx].state = LoSon
			if node.Item != nil && Intersect(g.extent, node.Size) {
				return node.Item, node.Size, true
			}
		case LoSon:
			g.stack[topIdx].state = HiSon
			if node.Sons[0] != nil {
				shouldPush := false
				if m >= 3 { // RIGHT, TOP or CEIL
					if g.extent[hort] <= node.Size[m] && g.extent[hort+3] >= node.LoMinBound {
						shouldPush = true
					}
				} else { // LEFT, BOTTOM or FLOOR
					if g.extent[hort] <= node.OtherBound && g.extent[hort+3] >= node.LoMinBound {
						shouldPush = true
					}
				}
				if shouldPush {
					g.stack = append(g.stack, save[T]{node: node.Sons[0], disc: nextDisc(m), state: ThisOne})
					continue
				}
			}
		case HiSon:
			g.stack[topIdx].state = Done
			if node.Sons[1] != nil {
				shouldPush := false
				if m >= 3 { // RIGHT, TOP or CEIL
					if g.extent[hort] <= node.HiMaxBound && g.extent[hort+3] >= node.OtherBound {
						shouldPush = true
					}
				} else { // LEFT, BOTTOM or FLOOR
					if g.extent[hort] <= node.HiMaxBound && g.extent[hort+3] >= node.Size[m] {
						shouldPush = true
					}
				}
				if shouldPush {
					g.stack = append(g.stack, save[T]{node: node.Sons[1], disc: nextDisc(m), state: ThisOne})
					continue
				}
			}
		case Done:
			g.stack = g.stack[:topIdx]
		}
	}
	return nil, Box[T]{}, false
}

func Intersect[T Coord](b1, b2 Box[T]) bool {
	return b1[Right] >= b2[Left] &&
		b2[Right] >= b1[Left] &&
		b1[Top] >= b2[Bottom] &&
		b2[Top] >= b1[Bottom] &&
		b1[Ceil] >= b2[Floor] &&
		b2[Ceil] >= b1[Floor]
}

// IsMember checks if an item exists in the tree
func (t *Tree[T]) IsMember(data interface{}, size Box[T]) bool {
	return t.findItem(t.Root, 0, data, size, true) != nil
}

// Count returns the number of active items in the tree
func (t *Tree[T]) Count() int {
	return t.ItemCount - t.DeadCount
}

// Delete marks an item as dead (soft delete)
func (t *Tree[T]) Delete(data interface{}, size Box[T]) bool {
	node := t.findItem(t.Root, 0, data, size, true)
	if node != nil && node.Item != nil {
		node.Item = nil
		t.DeadCount++
		return true
	}
	return false
}

// HardDelete physically removes an item from the tree and restructures it
func (t *Tree[T]) HardDelete(item interface{}, size Box[T]) bool {
	initialCount := t.ItemCount
	t.Root = t.hardDeleteRecursive(t.Root, 0, item, size)
	return t.ItemCount < initialCount
}

func (t *Tree[T]) hardDeleteRecursive(node *Node[T], disc int, item interface{}, size Box[T]) *Node[T] {
	if node == nil {
		return nil
	}

	if node.Item == item {
		if node.Sons[0] == nil && node.Sons[1] == nil {
			t.ItemCount--
			return nil
		}
		if node.Sons[1] != nil { // HISON exists, find MIN in target disc
			qItem, qSize := t.findExtreme(node.Sons[1], nextDisc(disc), disc, true)
			node.Item = qItem
			node.Size = qSize
			node.Sons[1] = t.hardDeleteRecursive(node.Sons[1], nextDisc(disc), qItem, qSize)
		} else { // LOSON exists, find MAX in target disc
			qItem, qSize := t.findExtreme(node.Sons[0], nextDisc(disc), disc, false)
			node.Item = qItem
			node.Size = qSize
			node.Sons[0] = t.hardDeleteRecursive(node.Sons[0], nextDisc(disc), qItem, qSize)
		}
		return node
	}

	val := size[disc] - node.Size[disc]
	next := nextDisc(disc)

	childIdx := 0
	if val == 0 {
		// Same tie ambiguity as `findItem` (see there for why) -- ask it which
		// side actually holds the item instead of guessing from this node's
		// other axes.
		if t.findItem(node.Sons[0], next, item, size, true) != nil {
			childIdx = 0
		} else {
			childIdx = 1
		}
	} else if val >= 0 {
		childIdx = 1
	}

	node.Sons[childIdx] = t.hardDeleteRecursive(node.Sons[childIdx], next, item, size)
	return node
}

func (t *Tree[T]) findExtreme(node *Node[T], nodeDisc int, targetDisc int, findMin bool) (interface{}, Box[T]) {
	bestItem := node.Item
	bestSize := node.Size

	searchLoson := node.Sons[0] != nil
	searchHison := node.Sons[1] != nil

	if nodeDisc == targetDisc {
		if findMin {
			searchHison = false // HISON elements are >= node, cannot be MIN
		} else {
			searchLoson = false // LOSON elements are < node, cannot be MAX
		}
	}

	if searchLoson {
		lItem, lSize := t.findExtreme(node.Sons[0], nextDisc(nodeDisc), targetDisc, findMin)
		if findMin {
			if lSize[targetDisc] < bestSize[targetDisc] {
				bestSize = lSize
				bestItem = lItem
			}
		} else {
			if lSize[targetDisc] > bestSize[targetDisc] {
				bestSize = lSize
				bestItem = lItem
			}
		}
	}

	if searchHison {
		hItem, hSize := t.findExtreme(node.Sons[1], nextDisc(nodeDisc), targetDisc, findMin)
		if findMin {
			if hSize[targetDisc] < bestSize[targetDisc] {
				bestSize = hSize
				bestItem = hItem
			}
		} else {
			if hSize[targetDisc] > bestSize[targetDisc] {
				bestSize = hSize
				bestItem = hItem
			}
		}
	}

	return bestItem, bestSize
}

// Priority represents an element for nearest neighbor search
type Priority struct {
	Dist float64
	Item interface{}
}

// Nearest finds the m closest items to the point (x, y, z)
func (t *Tree[T]) Nearest(x, y, z T, m int) []Priority {
	if t.Root == nil || m <= 0 {
		return nil
	}

	list := make([]Priority, m)
	for i := range list {
		list[i].Dist = math.MaxFloat64
	}

	Xq := Box[T]{x, y, z, x, y, z}
	
	var maxT T
	var minT T
	
	switch interface{}(x).(type) {
	case int:
		val := int(2147483647)
		maxT = T(val)
		minT = T(-val)
	case int32:
		val := int32(2147483647)
		maxT = T(val)
		minT = T(-val)
	case int64:
		valMax := int64(9223372036854775807)
		valMin := int64(-9223372036854775808)
		maxT = T(valMax)
		minT = T(valMin)
	case float64:
		maxT = T(math.Inf(1))
		minT = T(math.Inf(-1))
	}

	Bp := Box[T]{maxT, maxT, maxT, maxT, maxT, maxT}
	Bn := Box[T]{minT, minT, minT, minT, minT, minT}

	t.kdNeighbor(t.Root, Xq, m, list, Bp, Bn)

	// Convert squared distances to actual distances
	for i := range list {
		if list[i].Dist != math.MaxFloat64 {
			list[i].Dist = math.Sqrt(list[i].Dist)
		}
	}
	return list
}

type nSave[T Coord] struct {
	node  *Node[T]
	disc  int
	state State
	Bn    Box[T]
	Bp    Box[T]
}

func (t *Tree[T]) kdNeighbor(node *Node[T], Xq Box[T], m int, list []Priority, Bp, Bn Box[T]) {
	stack := make([]nSave[T], 0, 16)
	stack = append(stack, nSave[T]{node: node, disc: 0, state: ThisOne, Bn: Bn, Bp: Bp})

	for len(stack) > 0 {
		topIdx := len(stack) - 1
		top := &stack[topIdx]
		currNode := top.node
		d := top.disc
		p := currNode.Size[d]

		hort := d % 3
		vert := d >= 3

		switch top.state {
		case ThisOne:
			top.state = LoSon
			if currNode.Item != nil {
				t.addPriority(m, list, Xq, currNode)
			}
		case LoSon:
			top.state = HiSon
			// Try the side containing the query point first, or follow the tree structure
			// The C code has a specific order based on Xq[d] <= p
			if Xq[d] <= p {
				if currNode.Sons[0] != nil {
					oldBn := top.Bn[hort]
					oldBp := top.Bp[hort]
					if vert {
						top.Bp[hort] = currNode.Size[d]
						top.Bn[hort] = currNode.LoMinBound
					} else {
						top.Bp[hort] = currNode.OtherBound
						top.Bn[hort] = currNode.LoMinBound
					}

					if t.boundsOverlapBall(Xq, top.Bp, top.Bn, m, list) {
						stack = append(stack, nSave[T]{node: currNode.Sons[0], disc: nextDisc(d), state: ThisOne, Bn: top.Bn, Bp: top.Bp})
						top.Bn[hort] = oldBn
						top.Bp[hort] = oldBp
						continue
					}
					top.Bn[hort] = oldBn
					top.Bp[hort] = oldBp
				}
			} else {
				if currNode.Sons[1] != nil {
					oldBn := top.Bn[hort]
					oldBp := top.Bp[hort]
					if vert {
						top.Bp[hort] = currNode.HiMaxBound
						top.Bn[hort] = currNode.OtherBound
					} else {
						top.Bp[hort] = currNode.HiMaxBound
						top.Bn[hort] = currNode.Size[d]
					}

					if t.boundsOverlapBall(Xq, top.Bp, top.Bn, m, list) {
						stack = append(stack, nSave[T]{node: currNode.Sons[1], disc: nextDisc(d), state: ThisOne, Bn: top.Bn, Bp: top.Bp})
						top.Bn[hort] = oldBn
						top.Bp[hort] = oldBp
						continue
					}
					top.Bn[hort] = oldBn
					top.Bp[hort] = oldBp
				}
			}
		case HiSon:
			top.state = Done
			if Xq[d] <= p {
				if currNode.Sons[1] != nil {
					oldBn := top.Bn[hort]
					oldBp := top.Bp[hort]
					if vert {
						top.Bp[hort] = currNode.HiMaxBound
						top.Bn[hort] = currNode.OtherBound
					} else {
						top.Bp[hort] = currNode.HiMaxBound
						top.Bn[hort] = currNode.Size[d]
					}

					if t.boundsOverlapBall(Xq, top.Bp, top.Bn, m, list) {
						stack = append(stack, nSave[T]{node: currNode.Sons[1], disc: nextDisc(d), state: ThisOne, Bn: top.Bn, Bp: top.Bp})
						top.Bn[hort] = oldBn
						top.Bp[hort] = oldBp
						continue
					}
					top.Bn[hort] = oldBn
					top.Bp[hort] = oldBp
				}
			} else {
				if currNode.Sons[0] != nil {
					oldBn := top.Bn[hort]
					oldBp := top.Bp[hort]
					if vert {
						top.Bp[hort] = currNode.Size[d]
						top.Bn[hort] = currNode.LoMinBound
					} else {
						top.Bp[hort] = currNode.OtherBound
						top.Bn[hort] = currNode.LoMinBound
					}

					if t.boundsOverlapBall(Xq, top.Bp, top.Bn, m, list) {
						stack = append(stack, nSave[T]{node: currNode.Sons[0], disc: nextDisc(d), state: ThisOne, Bn: top.Bn, Bp: top.Bp})
						top.Bn[hort] = oldBn
						top.Bp[hort] = oldBp
						continue
					}
					top.Bn[hort] = oldBn
					top.Bp[hort] = oldBp
				}
			}
		case Done:
			stack = stack[:topIdx]
		}
	}
}

func (t *Tree[T]) addPriority(m int, list []Priority, Xq Box[T], node *Node[T]) {
	d := kdDist(Xq, node.Size)
	for x := m - 1; x >= 0; x-- {
		if d < list[x].Dist {
			if x != m-1 {
				list[x+1] = list[x]
			}
			list[x].Dist = d
			list[x].Item = node.Item
		} else {
			break
		}
	}
}

func (t *Tree[T]) boundsOverlapBall(Xq Box[T], Bp, Bn Box[T], m int, list []Priority) bool {
	var sum float64
	maxDist := list[m-1].Dist
	for i := 0; i < 3; i++ {
		if Xq[i] < Bn[i] {
			d := float64(Xq[i] - Bn[i])
			sum += d * d
			if sum > maxDist {
				return false
			}
		} else if Xq[i] > Bp[i] {
			d := float64(Xq[i] - Bp[i])
			sum += d * d
			if sum > maxDist {
				return false
			}
		}
	}
	return true
}

func kdDist[T Coord](Xq, box Box[T]) float64 {
	var dx, dy, dz float64

	if Xq[Left] > box[Right] {
		dx = float64(Xq[Left] - box[Right])
	} else if Xq[Right] < box[Left] {
		dx = float64(box[Left] - Xq[Right])
	}

	if Xq[Bottom] > box[Top] {
		dy = float64(Xq[Bottom] - box[Top])
	} else if Xq[Top] < box[Bottom] {
		dy = float64(box[Bottom] - Xq[Top])
	}

	if Xq[Floor] > box[Ceil] {
		dz = float64(Xq[Floor] - box[Ceil])
	} else if Xq[Ceil] < box[Floor] {
		dz = float64(box[Floor] - Xq[Ceil])
	}

	return dx*dx + dy*dy + dz*dz
}

func nodeCmp[T Coord](a, b *Node[T], disc int) bool {
	val := a.Size[disc] - b.Size[disc]
	if val == 0 {
		/* Cyclical comparison required */
		newDisc := nextDisc(disc)
		for newDisc != disc {
			val = a.Size[newDisc] - b.Size[newDisc]
			if val != 0 {
				break
			}
			newDisc = nextDisc(newDisc)
		}
		if val == 0 {
			val = 1
		}
	}
	return val >= 0
}

type findSave[T Coord] struct {
	node  *Node[T]
	disc  int
	state int
}

func (t *Tree[T]) findMinMaxNode(j int, kdMinvalNode **Node[T], kdMinvalNodesdad **Node[T], dir *int, newj *int) int {
	kdDataTries := 0
	stack := []findSave[T]{
		{
			node:  *kdMinvalNode,
			disc:  nextDisc(j),
			state: -1, // KD_THIS_ONE
		},
	}

	if *dir == 1 { // KD_HISON
		for len(stack) > 0 {
			topIdx := len(stack) - 1
			top := &stack[topIdx]
			topItem := top.node
			m := top.disc

			switch top.state {
			case -1: // KD_THIS_ONE
				kdDataTries++
				if topItem.Item != nil && !nodeCmp(topItem, *kdMinvalNode, j) && topItem != *kdMinvalNode {
					*kdMinvalNode = topItem
					*kdMinvalNodesdad = stack[len(stack)-2].node
					if *kdMinvalNode == (*kdMinvalNodesdad).Sons[0] {
						*dir = 0 // KD_LOSON
					} else {
						*dir = 1 // KD_HISON
					}
					*newj = m
					top.state++
				} else {
					top.state++
				}
			case 0: // KD_LOSON
				if topItem.Sons[0] != nil {
					top.state++
					stack = append(stack, findSave[T]{
						node:  topItem.Sons[0],
						disc:  nextDisc(m),
						state: -1,
					})
				} else {
					top.state++
				}
			case 1: // KD_HISON
				if j == m && topItem.Size[m] > (*kdMinvalNode).Size[m] {
					top.state++
				} else {
					if topItem.Sons[1] != nil {
						top.state++
						stack = append(stack, findSave[T]{
							node:  topItem.Sons[1],
							disc:  nextDisc(m),
							state: -1,
						})
					} else {
						top.state++
					}
				}
			default:
				stack = stack[:topIdx]
			}
		}
		return kdDataTries
	} else { // KD_LOSON
		for len(stack) > 0 {
			topIdx := len(stack) - 1
			top := &stack[topIdx]
			topItem := top.node
			m := top.disc

			switch top.state {
			case -1: // KD_THIS_ONE
				kdDataTries++
				if topItem.Item != nil && nodeCmp(topItem, *kdMinvalNode, j) && topItem != *kdMinvalNode {
					*kdMinvalNode = topItem
					*kdMinvalNodesdad = stack[len(stack)-2].node
					if *kdMinvalNode == (*kdMinvalNodesdad).Sons[0] {
						*dir = 0 // KD_LOSON
					} else {
						*dir = 1 // KD_HISON
					}
					*newj = m
					top.state++
				} else {
					top.state++
				}
			case 0: // KD_LOSON
				if j == m && topItem.Size[m] < (*kdMinvalNode).Size[m] {
					top.state++
				} else {
					if topItem.Sons[0] != nil {
						top.state++
						stack = append(stack, findSave[T]{
							node:  topItem.Sons[0],
							disc:  nextDisc(m),
							state: -1,
						})
					} else {
						top.state++
					}
				}
			case 1: // KD_HISON
				if topItem.Sons[1] != nil {
					top.state++
					stack = append(stack, findSave[T]{
						node:  topItem.Sons[1],
						disc:  nextDisc(m),
						state: -1,
					})
				} else {
					top.state++
				}
			default:
				stack = stack[:topIdx]
			}
		}
		return kdDataTries
	}
}

var deleteFlip bool

type deleteStats struct {
	numTries int
	numDel   int
}

func (t *Tree[T]) findItemWithPath(node *Node[T], disc int, item interface{}, size Box[T], path []*Node[T]) (*Node[T], []*Node[T]) {
	if node == nil {
		return nil, path
	}
	if item == node.Item {
		return node, path
	}

	val := size[disc] - node.Size[disc]

	if val == 0 {
		// Same tie ambiguity as `findItem` -- try both sides instead of
		// guessing via this node's other axes, since kdDoDelete's promotion
		// step can swap a different item into this position later. append
		// on the unchanged `path` for each attempt, so a failed attempt's
		// pushes (possibly sharing backing storage) never leak into the
		// path returned by whichever side actually holds the item.
		if found, newPath := t.findItemWithPath(node.Sons[0], nextDisc(disc), item, size, append(path, node)); found != nil {
			return found, newPath
		}
		return t.findItemWithPath(node.Sons[1], nextDisc(disc), item, size, append(path, node))
	}

	childIdx := 0
	if val >= 0 {
		childIdx = 1
	}

	if node.Sons[childIdx] != nil {
		newPath := append(path, node)
		return t.findItemWithPath(node.Sons[childIdx], nextDisc(disc), item, size, newPath)
	}

	return nil, path
}

func (t *Tree[T]) kdDoDelete(elem *Node[T], j int, stats *deleteStats) *Node[T] {
	deleteFlip = !deleteFlip

	if elem.Sons[0] == nil && elem.Sons[1] == nil {
		return nil
	}

	var Q, Qdad *Node[T]
	var Qson int
	var newj int

	Qdad = elem
	if elem.Sons[1] == nil {
		deleteFlip = false
	} else if elem.Sons[0] == nil {
		deleteFlip = true
	}

	if !deleteFlip { // loson
		Q = elem.Sons[0]
		Qson = 0
		newj = nextDisc(j)
		stats.numTries += t.findMinMaxNode(j, &Q, &Qdad, &Qson, &newj)
	} else { // hison
		Q = elem.Sons[1]
		Qson = 1
		newj = nextDisc(j)
		stats.numTries += t.findMinMaxNode(j, &Q, &Qdad, &Qson, &newj)
	}

	Qdad.Sons[Qson] = t.kdDoDelete(Q, newj, stats)
	stats.numDel++
	Q.Sons[0] = elem.Sons[0]
	Q.Sons[1] = elem.Sons[1]
	Q.LoMinBound = elem.LoMinBound
	Q.OtherBound = elem.OtherBound
	Q.HiMaxBound = elem.HiMaxBound
	return Q
}

// ReallyDelete structurally deletes an item from the tree, returning its status, the number of tries, and number of deletions.
func (t *Tree[T]) ReallyDelete(data interface{}, oldSize Box[T]) (Status, int, int) {
	elem, path := t.findItemWithPath(t.Root, 0, data, oldSize, nil)
	if elem == nil {
		return NotFound, 0, 0
	}

	stats := &deleteStats{
		numTries: 0,
		numDel:   1, // kd_really_delete sets kddel_number_deld = 1 initially
	}

	if elem == t.Root {
		t.Root = t.kdDoDelete(elem, 0, stats)
	} else {
		parent := path[len(path)-1]
		j := len(path) % 6
		newElem := t.kdDoDelete(elem, j, stats)
		if parent.Sons[1] == elem {
			parent.Sons[1] = newElem
		} else {
			parent.Sons[0] = newElem
		}
	}

	t.ItemCount--
	return OK, stats.numTries, stats.numDel
}

// Badness prints the balance ratio, count of single branch nodes, max depth, and dead node statistics to stdout.
func (t *Tree[T]) Badness() {
	factor3 := 0
	maxLevels := 0

	var stats func(node *Node[T], level int)
	stats = func(node *Node[T], level int) {
		if node == nil {
			return
		}
		if (node.Sons[0] != nil || node.Sons[1] != nil) && !(node.Sons[0] != nil && node.Sons[1] != nil) {
			factor3++
		}
		if level > maxLevels {
			maxLevels = level
		}
		stats(node.Sons[0], level+1)
		stats(node.Sons[1], level+1)
	}

	stats(t.Root, 1)

	var targdepth float64
	if t.ItemCount > 0 {
		targdepth = math.Log2(float64(t.ItemCount))
		targdepth = math.Floor(targdepth)
		targdepth++
	}

	var ratio float64
	if targdepth > 0 {
		ratio = float64(maxLevels) / targdepth
	}

	var deadPct float64
	if t.ItemCount > 0 {
		deadPct = (float64(t.DeadCount) / float64(t.ItemCount)) * 100.0
	}

	var factor3Pct float64
	if t.ItemCount > 0 {
		factor3Pct = (float64(factor3) / float64(t.ItemCount)) * 100.0
	}

	fmt.Printf("balance ratio=%g (the closer to 1.0, the better), #of nodes with only one branch=%d (%g), max depth=%d, dead=%d (%g)\n",
		ratio, factor3, factor3Pct, maxLevels, t.DeadCount, deadPct)
}

type MmapNode[T Coord] struct {
	SourceID   uint64
	Size       Box[T]
	LoMinBound T
	HiMaxBound T
	OtherBound T
	LeftChild  int64
	RightChild int64
}

func writeT[T Coord](file *os.File, val T) {
	switch v := any(val).(type) {
	case int32:
		binary.Write(file, binary.LittleEndian, v)
	case int64:
		binary.Write(file, binary.LittleEndian, v)
	case int:
		binary.Write(file, binary.LittleEndian, int64(v))
	case float64:
		binary.Write(file, binary.LittleEndian, v)
	}
}

func (t *Tree[T]) Serialize(filename string, itemToID func(interface{}) uint64) error {
	count := t.ItemCount - t.DeadCount
	if count <= 0 {
		return fmt.Errorf("Empty tree")
	}

	file, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer file.Close()

	vec := make([]MmapNode[T], 0, count)

	var serializeNodeRecursive func(node *Node[T]) int64
	serializeNodeRecursive = func(node *Node[T]) int64 {
		if node != nil && node.Item != nil {
			myIdx := int64(len(vec))
			vec = append(vec, MmapNode[T]{})
			
			left := serializeNodeRecursive(node.Sons[0])
			right := serializeNodeRecursive(node.Sons[1])
			
			vec[myIdx] = MmapNode[T]{
				SourceID:   itemToID(node.Item),
				Size:       node.Size,
				LoMinBound: node.LoMinBound,
				HiMaxBound: node.HiMaxBound,
				OtherBound: node.OtherBound,
				LeftChild:  left,
				RightChild: right,
			}
			return myIdx
		}
		return -1
	}

	serializeNodeRecursive(t.Root)

	for i := range vec {
		binary.Write(file, binary.LittleEndian, vec[i].SourceID)
		for j := 0; j < len(vec[i].Size); j++ {
			writeT(file, vec[i].Size[j])
		}
		writeT(file, vec[i].LoMinBound)
		writeT(file, vec[i].HiMaxBound)
		writeT(file, vec[i].OtherBound)
		binary.Write(file, binary.LittleEndian, vec[i].LeftChild)
		binary.Write(file, binary.LittleEndian, vec[i].RightChild)
	}

	return nil
}

// GetBounds calculates the exact bounding box of all active nodes in the tree
func (t *Tree[T]) GetBounds() (Box[T], error) {
	var bounds Box[T]
	if t.Root == nil {
		return bounds, fmt.Errorf("Tree is empty")
	}

	first := true
	var traverse func(node *Node[T])
	traverse = func(node *Node[T]) {
		if node == nil {
			return
		}
		if node.Item != nil {
			if first {
				bounds = node.Size
				first = false
			} else {
				for d := 0; d < len(bounds)/2; d++ {
					if node.Size[d] < bounds[d] {
						bounds[d] = node.Size[d]
					}
					if node.Size[d+len(bounds)/2] > bounds[d+len(bounds)/2] {
						bounds[d+len(bounds)/2] = node.Size[d+len(bounds)/2]
					}
				}
			}
		}
		traverse(node.Sons[0])
		traverse(node.Sons[1])
	}

	traverse(t.Root)
	if first {
		return bounds, fmt.Errorf("No active items in tree")
	}
	return bounds, nil
}

func sizeofT[T Coord]() int {
	var val T
	switch any(val).(type) {
	case int32:
		return 4
	case int64, int:
		return 8
	case float64:
		return 8
	default:
		return 8
	}
}

// GetMmapBounds calculates the bounding box directly from an array of MmapNode
func GetMmapBounds[T Coord](nodes []MmapNode[T]) (Box[T], error) {
	var bounds Box[T]
	if len(nodes) == 0 {
		return bounds, fmt.Errorf("Nodes array is empty")
	}

	dim := len(bounds) / 2
	first := true
	for i := range nodes {
		if nodes[i].SourceID != 0 {
			if first {
				bounds = nodes[i].Size
				first = false
			} else {
				for d := 0; d < dim; d++ {
					if nodes[i].Size[d] < bounds[d] {
						bounds[d] = nodes[i].Size[d]
					}
					if nodes[i].Size[d+dim] > bounds[d+dim] {
						bounds[d+dim] = nodes[i].Size[d+dim]
					}
				}
			}
		}
	}

	if first {
		return bounds, fmt.Errorf("No active items in nodes array")
	}
	return bounds, nil
}

// GetSerializedBounds calculates the bounding box directly from a serialized .kdtree file
func GetSerializedBounds[T Coord](filename string) (Box[T], error) {
	var bounds Box[T]
	file, err := os.Open(filename)
	if err != nil {
		return bounds, err
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		return bounds, err
	}

	dim := len(bounds) / 2
	tSize := sizeofT[T]()
	var zero T
	_, isFloat := any(zero).(float64)
	recordSize := int64(8 + (2*dim+3)*tSize + 16)
	if info.Size()%recordSize != 0 {
		return bounds, fmt.Errorf("Invalid file size")
	}

	nodeCount := info.Size() / recordSize
	if nodeCount == 0 {
		return bounds, fmt.Errorf("File is empty")
	}

	// Try O(1) fast retrieval: Seek to the last record to check for our sentinel node
	if _, err := file.Seek(-recordSize, 2); err == nil {
		var sourceID uint64
		if err := binary.Read(file, binary.LittleEndian, &sourceID); err == nil {
			if sourceID == ^uint64(0) {
				var size Box[T]
				for j := 0; j < 2*dim; j++ {
					var val T
					if isFloat {
						var bits uint64
						binary.Read(file, binary.LittleEndian, &bits)
						val = T(math.Float64frombits(bits))
					} else if tSize == 4 {
						var val32 int32
						binary.Read(file, binary.LittleEndian, &val32)
						val = T(val32)
					} else {
						var val64 int64
						binary.Read(file, binary.LittleEndian, &val64)
						val = T(val64)
					}
					size[j] = val
				}
				return size, nil
			}
		}
	}

	// Reset file offset to beginning for O(N) fallback scan
	if _, err := file.Seek(0, 0); err != nil {
		return bounds, err
	}

	nodes := make([]MmapNode[T], nodeCount)
	for i := int64(0); i < nodeCount; i++ {
		var sourceID uint64
		if err := binary.Read(file, binary.LittleEndian, &sourceID); err != nil {
			return bounds, err
		}

		var size Box[T]
		for j := 0; j < 2*dim; j++ {
			var val T
			if isFloat {
				var bits uint64
				binary.Read(file, binary.LittleEndian, &bits)
				val = T(math.Float64frombits(bits))
			} else if tSize == 4 {
				var val32 int32
				binary.Read(file, binary.LittleEndian, &val32)
				val = T(val32)
			} else {
				var val64 int64
				binary.Read(file, binary.LittleEndian, &val64)
				val = T(val64)
			}
			size[j] = val
		}

		var loMin, hiMax, other T
		if isFloat {
			var bits uint64
			binary.Read(file, binary.LittleEndian, &bits); loMin = T(math.Float64frombits(bits))
			binary.Read(file, binary.LittleEndian, &bits); hiMax = T(math.Float64frombits(bits))
			binary.Read(file, binary.LittleEndian, &bits); other = T(math.Float64frombits(bits))
		} else if tSize == 4 {
			var val32 int32
			binary.Read(file, binary.LittleEndian, &val32); loMin = T(val32)
			binary.Read(file, binary.LittleEndian, &val32); hiMax = T(val32)
			binary.Read(file, binary.LittleEndian, &val32); other = T(val32)
		} else {
			var val64 int64
			binary.Read(file, binary.LittleEndian, &val64); loMin = T(val64)
			binary.Read(file, binary.LittleEndian, &val64); hiMax = T(val64)
			binary.Read(file, binary.LittleEndian, &val64); other = T(val64)
		}

		var left, right int64
		if err := binary.Read(file, binary.LittleEndian, &left); err != nil {
			return bounds, err
		}
		if err := binary.Read(file, binary.LittleEndian, &right); err != nil {
			return bounds, err
		}

		nodes[i] = MmapNode[T]{
			SourceID:   sourceID,
			Size:       size,
			LoMinBound: loMin,
			HiMaxBound: hiMax,
			OtherBound: other,
			LeftChild:  left,
			RightChild: right,
		}
	}

	return GetMmapBounds(nodes)
}
