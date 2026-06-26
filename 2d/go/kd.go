package kd

import (
	"fmt"
	"math"
)

type Coord interface {
	~int | ~int32 | ~int64
}

// Box defines a 2D bounding box [left, bottom, right, top]
type Box[T Coord] [4]T

const (
	Left   = 0
	Bottom = 1
	Right  = 2
	Top    = 3
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
	Item       interface{}
	Size       Box[T]
	LoMinBound T
	HiMaxBound T
	OtherBound T
	Sons       [2]*Node[T]
}

// Tree represents the KD-tree structure
type Tree[T Coord] struct {
	Root          *Node[T]
	ItemCount     int
	DeadCount     int
	Extent        Box[T]
	ItemsBalanced int
}

// Create returns a new empty KD-tree
func Create[T Coord]() *Tree[T] {
	return &Tree[T]{}
}

// Disc returns the discriminator for a given level
func Disc(level int) int {
	return level % 4
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
			HiMaxBound: size[2],
			OtherBound: size[0],
		}
		t.Extent = size
		t.ItemCount = 1
		return
	}

	if t.findItem(t.Root, 0, data, size, false) != nil {
		t.ItemCount++
		// Update tree extent
		if size[Left] < t.Extent[Left] {
			t.Extent[Left] = size[Left]
		}
		if size[Right] > t.Extent[Right] {
			t.Extent[Right] = size[Right]
		}
		if size[Top] > t.Extent[Top] {
			t.Extent[Top] = size[Top]
		}
		if size[Bottom] < t.Extent[Bottom] {
			t.Extent[Bottom] = size[Bottom]
		}
	}
}

func nextDisc(disc int) int {
	return (disc + 1) % 4
}

func (t *Tree[T]) findItem(elem *Node[T], disc int, item interface{}, size Box[T], searchP bool) *Node[T] {
	if item == elem.Item {
		if searchP {
			return elem
		}
		return nil // Duplicate not allowed for insert
	}

	val := size[disc] - elem.Size[disc]
	if val == 0 {
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
	vert := nextDisc(disc) & 0x01
	newNode := &Node[T]{
		Item:       item,
		Size:       size,
		LoMinBound: size[vert],
		HiMaxBound: size[vert+2],
	}
	// Logic for other_bound from C code:
	// items_elem->other_bound = ((NEXTDISC(disc)&0x2) ? size[vert] : size[vert+2]);
	if (nextDisc(disc) & 0x02) != 0 {
		newNode.OtherBound = size[vert]
	} else {
		newNode.OtherBound = size[vert+2]
	}

	elem.Sons[childIdx] = newNode
	t.boundsUpdate(elem, disc, size)
	return newNode
}

func (t *Tree[T]) boundsUpdate(elem *Node[T], disc int, size Box[T]) {
	vert := disc & 0x01
	if size[vert] < elem.LoMinBound {
		elem.LoMinBound = size[vert]
	}
	if size[vert+2] > elem.HiMaxBound {
		elem.HiMaxBound = size[vert+2]
	}
	if (disc & 0x02) != 0 {
		if size[vert] < elem.OtherBound {
			elem.OtherBound = size[vert]
		}
	} else {
		if size[vert+2] > elem.OtherBound {
			elem.OtherBound = size[vert+2]
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
		hort := m & 0x01

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
				if (m & 0x02) != 0 { // RIGHT or TOP
					if g.extent[hort] <= node.Size[m] && g.extent[hort+2] >= node.LoMinBound {
						shouldPush = true
					}
				} else { // LEFT or BOTTOM
					if g.extent[hort] <= node.OtherBound && g.extent[hort+2] >= node.LoMinBound {
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
				if (m & 0x02) != 0 { // RIGHT or TOP
					if g.extent[hort] <= node.HiMaxBound && g.extent[hort+2] >= node.OtherBound {
						shouldPush = true
					}
				} else { // LEFT or BOTTOM
					if g.extent[hort] <= node.HiMaxBound && g.extent[hort+2] >= node.Size[m] {
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
		b2[Top] >= b1[Bottom]
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
	if val == 0 {
		ndisc := nextDisc(disc)
		for ndisc != disc {
			val = size[ndisc] - node.Size[ndisc]
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

	node.Sons[childIdx] = t.hardDeleteRecursive(node.Sons[childIdx], nextDisc(disc), item, size)
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
		ndisc := nextDisc(disc)
		for ndisc != disc {
			val = size[ndisc] - node.Size[ndisc]
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
		j := len(path) % 4
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

// Priority represents an element for nearest neighbor search
type Priority struct {
	Dist float64
	Item interface{}
}

// Nearest finds the m closest items to the point (x, y)
func (t *Tree[T]) Nearest(x, y T, m int) []Priority {
	if t.Root == nil || m <= 0 {
		return nil
	}

	list := make([]Priority, m)
	for i := range list {
		list[i].Dist = math.MaxFloat64
	}

	Xq := Box[T]{x, y, x, y}
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
	}

	Bp := Box[T]{maxT, maxT, maxT, maxT}
	Bn := Box[T]{minT, minT, minT, minT}

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

		hort := d & 1
		vert := (d & 2) != 0

		switch top.state {
		case ThisOne:
			top.state = LoSon
			if currNode.Item != nil {
				t.addPriority(m, list, Xq, currNode)
			}
		case LoSon:
			top.state = HiSon
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
	for i := 0; i < 2; i++ {
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
	var dx, dy float64

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

	return dx*dx + dy*dy
}
