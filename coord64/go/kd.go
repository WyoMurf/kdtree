package kd


// Box defines a 2D bounding box [left, bottom, right, top]
// Box defines a 2D bounding box [left, bottom, right, top]
type Box [4]int64

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
type Node struct {
	Item         interface{}
	Size         Box
	LoMinBound   int64
	HiMaxBound   int64
	OtherBound   int64
	Sons         [2]*Node
}

// Tree represents the KD-tree structure
type Tree struct {
	Root           *Node
	ItemCount      int
	DeadCount      int
	Extent         Box
	ItemsBalanced  int
}

// Create returns a new empty KD-tree
func Create() *Tree {
	return &Tree{}
}

// Disc returns the discriminator for a given level
func Disc(level int) int {
	return level % 4
}

// ... more implementation to follow

func (t *Tree) Insert(data interface{}, size Box) {
	if data == nil {
		panic("KD: attempt to insert nil data")
	}

	if t.Root == nil {
		t.Root = &Node{
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
		if size[Left] < t.Extent[Left] { t.Extent[Left] = size[Left] }
		if size[Right] > t.Extent[Right] { t.Extent[Right] = size[Right] }
		if size[Top] > t.Extent[Top] { t.Extent[Top] = size[Top] }
		if size[Bottom] < t.Extent[Bottom] { t.Extent[Bottom] = size[Bottom] }
	}
}

func nextDisc(disc int) int {
	return (disc + 1) % 4
}

func (t *Tree) findItem(elem *Node, disc int, item interface{}, size Box, searchP bool) *Node {
	if item == elem.Item {
		if searchP {
			return elem
		}
		return nil // Duplicate not allowed for insert
	}

	var val int64 = size[disc] - elem.Size[disc]
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
	vert := int(nextDisc(disc) & 0x01)
	newNode := &Node{
		Item:       item,
		Size:       size,
		LoMinBound: size[vert],
		HiMaxBound: size[vert+2],
	}
	// Logic for other_bound from C code: 
	// items_elem->other_bound = ((NEXTDISC(disc)&0x2) ? size[vert] : size[vert+2]);
	if (nextDisc(disc) & 0x2) != 0 {
		newNode.OtherBound = size[vert]
	} else {
		newNode.OtherBound = size[vert+2]
	}

	elem.Sons[childIdx] = newNode
	t.boundsUpdate(elem, disc, size)
	return newNode
}

func (t *Tree) boundsUpdate(elem *Node, disc int, size Box) {
	vert := int(disc & 0x01)
	if size[vert] < elem.LoMinBound { elem.LoMinBound = size[vert] }
	if size[vert+2] > elem.HiMaxBound { elem.HiMaxBound = size[vert+2] }
	if (disc & 0x02) != 0 {
		if size[vert] < elem.OtherBound { elem.OtherBound = size[vert] }
	} else {
		if size[vert+2] > elem.OtherBound { elem.OtherBound = size[vert+2] }
	}
}

type State int

const (
	ThisOne State = 0
	LoSon   State = 1
	HiSon   State = 2
	Done    State = 3
)

type save struct {
	node  *Node
	disc  int
	state State
}

type Generator struct {
	extent Box
	stack  []save
}

func (t *Tree) Start(area Box) *Generator {
	g := &Generator{
		extent: area,
	}
	if t.Root != nil {
		g.stack = append(g.stack, save{node: t.Root, disc: 0, state: ThisOne})
	}
	return g
}

func (g *Generator) Next() (interface{}, Box, bool) {
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
					g.stack = append(g.stack, save{node: node.Sons[0], disc: nextDisc(m), state: ThisOne})
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
					g.stack = append(g.stack, save{node: node.Sons[1], disc: nextDisc(m), state: ThisOne})
					continue
				}
			}
		case Done:
			g.stack = g.stack[:topIdx]
		}
	}
	return nil, Box{}, false
}

func Intersect(b1, b2 Box) bool {
	return b1[Right] >= b2[Left] &&
		b2[Right] >= b1[Left] &&
		b1[Top] >= b2[Bottom] &&
		b2[Top] >= b1[Bottom]
}

// IsMember checks if an item exists in the tree
func (t *Tree) IsMember(data interface{}, size Box) bool {
	return t.findItem(t.Root, 0, data, size, true) != nil
}

// Count returns the number of active items in the tree
func (t *Tree) Count() int {
	return t.ItemCount - t.DeadCount
}

// Delete marks an item as dead (soft delete)
func (t *Tree) Delete(data interface{}, size Box) bool {
	node := t.findItem(t.Root, 0, data, size, true)
	if node != nil && node.Item != nil {
		node.Item = nil
		t.DeadCount++
		return true
	}
	return false
}

// HardDelete physically removes an item from the tree and restructures it
func (t *Tree) HardDelete(item interface{}, size Box) bool {
	initialCount := t.ItemCount
	t.Root = t.hardDeleteRecursive(t.Root, 0, item, size)
	return t.ItemCount < initialCount
}

func (t *Tree) hardDeleteRecursive(node *Node, disc int, item interface{}, size Box) *Node {
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

func (t *Tree) findExtreme(node *Node, nodeDisc int, targetDisc int, findMin bool) (interface{}, Box) {
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
