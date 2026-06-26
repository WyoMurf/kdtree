use std::cmp::{min, max};

pub trait Coord: Copy + Ord + std::ops::Sub<Output = Self> + std::ops::Add<Output = Self> + Default {
    fn zero() -> Self { Self::default() }
    fn from_i32(val: i32) -> Self;
    fn min_value() -> Self;
    fn max_value() -> Self;
    fn to_f64(self) -> f64;
}

impl Coord for i32 {
    #[inline]
    fn from_i32(val: i32) -> Self { val }
    #[inline]
    fn min_value() -> Self { i32::MIN }
    #[inline]
    fn max_value() -> Self { i32::MAX }
    #[inline]
    fn to_f64(self) -> f64 { self as f64 }
}

impl Coord for i64 {
    #[inline]
    fn from_i32(val: i32) -> Self { val as i64 }
    #[inline]
    fn min_value() -> Self { i64::MIN }
    #[inline]
    fn max_value() -> Self { i64::MAX }
    #[inline]
    fn to_f64(self) -> f64 { self as f64 }
}

// Box defines a 3D bounding box [left, bottom, floor, right, top, ceil]
pub type KdBox<C = i32> = [C; 6];

pub const LEFT: usize = 0;
pub const BOTTOM: usize = 1;
pub const FLOOR: usize = 2;
pub const RIGHT: usize = 3;
pub const TOP: usize = 4;
pub const CEIL: usize = 5;

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Status {
    Ok = 1,
    NoMore = 2,
    NotImpl = -3,
    NotFound = -4,
}

pub struct Node<T, C = i32> {
    pub item: Option<T>,
    pub size: KdBox<C>,
    pub lo_min_bound: C,
    pub hi_max_bound: C,
    pub other_bound: C,
    pub sons: [Option<usize>; 2],
}

/// A 3D KD-Tree implementation using arena allocation.
///
/// The tree stores items of type `T` associated with a 6-element bounding box
/// `[left, bottom, floor, right, top, ceil]`.
/// It uses a `Vec`-backed arena for node storage to improve performance and cache locality.
pub struct Tree<T, C = i32> {
    pub arena: Vec<Node<T, C>>,
    pub free_list: Vec<usize>,
    pub root: Option<usize>,
    pub item_count: i32,
    pub dead_count: i32,
    pub extent: KdBox<C>,
    pub delete_flip: bool,
}

#[derive(Clone, Copy, PartialEq)]
enum State { ThisOne, LoSon, HiSon, Done }

struct Save<C> {
    node_idx: usize,
    disc: usize,
    state: State,
    bn: KdBox<C>,
    bp: KdBox<C>,
}

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    /// Creates a new, empty 3D KD-Tree.
    pub fn new() -> Self {
        Self {
            arena: Vec::new(),
            free_list: Vec::new(),
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [C::zero(); 6],
            delete_flip: false,
        }
    }

    /// Creates a new 3D KD-Tree with a pre-allocated capacity for the node arena.
    pub fn with_capacity(capacity: usize) -> Self {
        Self {
            arena: Vec::with_capacity(capacity),
            free_list: Vec::new(),
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [C::zero(); 6],
            delete_flip: false,
        }
    }

    fn allocate_node(&mut self, node: Node<T, C>) -> usize {
        if let Some(index) = self.free_list.pop() {
            self.arena[index] = node;
            index
        } else {
            self.arena.push(node);
            self.arena.len() - 1
        }
    }

    fn free_node(&mut self, index: usize) {
        self.arena[index].item = None;
        self.free_list.push(index);
    }

    /// Inserts an item into the tree with the given 3D bounding box.
    pub fn insert(&mut self, item: T, size: KdBox<C>) {
        if self.root.is_none() {
            let node = Node {
                item: Some(item),
                size,
                lo_min_bound: size[0],
                hi_max_bound: size[3],
                other_bound: size[0],
                sons: [None, None],
            };
            self.root = Some(self.allocate_node(node));
            self.extent = size;
            self.item_count = 1;
            return;
        }

        let root_idx = self.root.unwrap();
        if self.insert_recursive(root_idx, 0, item, &size) {
            self.item_count += 1;
            for i in 0..3 {
                self.extent[i] = min(self.extent[i], size[i]);
                self.extent[i + 3] = max(self.extent[i + 3], size[i + 3]);
            }
        }
    }

    fn insert_recursive(&mut self, node_idx: usize, disc: usize, item: T, size: &KdBox<C>) -> bool {
        if let Some(ref node_item) = self.arena[node_idx].item {
            if item == *node_item {
                return false;
            }
        }

        let mut val = size[disc] - self.arena[node_idx].size[disc];
        if val == C::zero() {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - self.arena[node_idx].size[ndisc];
                if val != C::zero() {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == C::zero() {
                val = C::from_i32(1);
            }
        }

        let child_idx = if val >= C::zero() { 1 } else { 0 };

        if let Some(child_child_idx) = self.arena[node_idx].sons[child_idx] {
            let inserted = self.insert_recursive(child_child_idx, next_disc(disc), item, size);
            if inserted {
                self.bounds_update(node_idx, disc, size);
            }
            inserted
        } else {
            let vert = next_disc(disc) % 3;
            let mut new_node = Node {
                item: Some(item),
                size: *size,
                lo_min_bound: size[vert],
                hi_max_bound: size[vert + 3],
                other_bound: C::zero(),
                sons: [None, None],
            };

            if next_disc(disc) >= 3 {
                new_node.other_bound = size[vert];
            } else {
                new_node.other_bound = size[vert + 3];
            }

            let new_idx = self.allocate_node(new_node);
            self.arena[node_idx].sons[child_idx] = Some(new_idx);
            self.bounds_update(node_idx, disc, size);
            true
        }
    }

    fn bounds_update(&mut self, node_idx: usize, disc: usize, size: &KdBox<C>) {
        let vert = disc % 3;
        self.arena[node_idx].lo_min_bound = min(self.arena[node_idx].lo_min_bound, size[vert]);
        self.arena[node_idx].hi_max_bound = max(self.arena[node_idx].hi_max_bound, size[vert + 3]);
        if disc >= 3 {
            self.arena[node_idx].other_bound = min(self.arena[node_idx].other_bound, size[vert]);
        } else {
            self.arena[node_idx].other_bound = max(self.arena[node_idx].other_bound, size[vert + 3]);
        }
    }

    fn find_recursive(&self, node_idx: Option<usize>, disc: usize, item: &T, size: &KdBox<C>) -> Option<usize> {
        let idx = node_idx?;
        let node = &self.arena[idx];

        if let Some(ref node_item) = node.item {
            if *item == *node_item {
                return Some(idx);
            }
        }

        let mut val = size[disc] - node.size[disc];
        if val == C::zero() {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - node.size[ndisc];
                if val != C::zero() {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == C::zero() {
                val = C::from_i32(1);
            }
        }

        let child_idx = if val >= C::zero() { 1 } else { 0 };
        self.find_recursive(node.sons[child_idx], next_disc(disc), item, size)
    }

    /// Checks if the given item is stored in the tree with the specified bounding box.
    pub fn is_member(&self, item: &T, size: &KdBox<C>) -> bool {
        self.find_recursive(self.root, 0, item, size).is_some()
    }

    /// Marks an item as deleted (soft delete).
    pub fn delete(&mut self, item: &T, size: &KdBox<C>) -> bool {
        if let Some(node_idx) = self.find_recursive(self.root, 0, item, size) {
            if self.arena[node_idx].item.is_some() {
                self.arena[node_idx].item = None;
                self.dead_count += 1;
                return true;
            }
        }
        false
    }

    /// Physically deletes an item from the tree and restructures the nodes (hard delete).
    pub fn hard_delete(&mut self, item: &T, size: &KdBox<C>) -> bool {
        let initial_count = self.item_count;
        if self.root.is_none() {
            return false;
        }

        let root_idx = self.root.unwrap();
        self.root = self.hard_delete_recursive(Some(root_idx), 0, item, size);
        self.item_count < initial_count
    }

    fn hard_delete_recursive(&mut self, node_idx: Option<usize>, disc: usize, item: &T, size: &KdBox<C>) -> Option<usize> {
        let elem_idx = node_idx?;
        
        let is_match = match self.arena[elem_idx].item {
            Some(ref node_item) => node_item == item,
            None => false,
        };

        if is_match {
            let mut stats = (0, 0); // (tries, del)
            let q_opt = self.kd_do_delete(elem_idx, disc, &mut stats);
            self.item_count -= 1;
            return q_opt;
        }

        let mut val = size[disc] - self.arena[elem_idx].size[disc];
        if val == C::zero() {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - self.arena[elem_idx].size[ndisc];
                if val != C::zero() {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == C::zero() {
                val = C::from_i32(1);
            }
        }

        let child_idx = if val >= C::zero() { 1 } else { 0 };
        self.arena[elem_idx].sons[child_idx] = self.hard_delete_recursive(self.arena[elem_idx].sons[child_idx], next_disc(disc), item, size);
        Some(elem_idx)
    }

    fn kd_do_delete(&mut self, elem_idx: usize, disc: usize, stats: &mut (i32, i32)) -> Option<usize> {
        self.delete_flip = !self.delete_flip;

        if self.arena[elem_idx].sons[0].is_none() && self.arena[elem_idx].sons[1].is_none() {
            self.free_node(elem_idx);
            return None;
        }

        let mut q_idx: usize;
        let mut q_dad_idx: usize = elem_idx;
        let mut q_son: usize = 0;
        let mut newj: usize = 0;
        
        if self.arena[elem_idx].sons[1].is_none() {
            self.delete_flip = false;
        } else if self.arena[elem_idx].sons[0].is_none() {
            self.delete_flip = true;
        }

        if !self.delete_flip {
            q_idx = self.arena[elem_idx].sons[0].unwrap();
            q_son = 0;
            newj = next_disc(disc);
            stats.0 += self.find_min_max_node(disc, &mut q_idx, &mut q_dad_idx, &mut q_son, &mut newj, false);
        } else {
            q_idx = self.arena[elem_idx].sons[1].unwrap();
            q_son = 1;
            newj = next_disc(disc);
            stats.num_tries_add(t_find_min_max_node_idx_tries(self, disc, &mut q_idx, &mut q_dad_idx, &mut q_son, &mut newj, true));
        }

        let q_replacement = self.kd_do_delete(q_idx, newj, stats);
        self.arena[q_dad_idx].sons[q_son] = q_replacement;
        stats.1 += 1;
        
        // Transfer elem data to q_idx
        self.arena[q_idx].sons[0] = self.arena[elem_idx].sons[0];
        self.arena[q_idx].sons[1] = self.arena[elem_idx].sons[1];
        self.arena[q_idx].lo_min_bound = self.arena[elem_idx].lo_min_bound;
        self.arena[q_idx].other_bound = self.arena[elem_idx].other_bound;
        self.arena[q_idx].hi_max_bound = self.arena[elem_idx].hi_max_bound;
        
        self.free_node(elem_idx);
        Some(q_idx)
    }

    fn find_min_max_node(&self, j: usize, kd_minval_node_idx: &mut usize, kd_minval_nodesdad_idx: &mut usize, dir: &mut usize, newj: &mut usize, find_min: bool) -> i32 {
        let mut kd_data_tries = 0;
        let mut stack = vec![(*kd_minval_node_idx, next_disc(j), -1, 0)]; // (node_idx, m, state, dad_idx)

        while let Some((node_idx, m, state, dad_idx)) = stack.pop() {
            let node = &self.arena[node_idx];
            match state {
                -1 => {
                    kd_data_tries += 1;
                    let is_better = if find_min {
                        node.size[j] < self.arena[*kd_minval_node_idx].size[j]
                    } else {
                        node.size[j] > self.arena[*kd_minval_node_idx].size[j]
                    };

                    if is_better && node_idx != *kd_minval_node_idx {
                        *kd_minval_node_idx = node_idx;
                        *kd_minval_nodesdad_idx = dad_idx;
                        let dad = &self.arena[*kd_minval_nodesdad_idx];
                        *dir = if dad.sons[0] == Some(node_idx) { 0 } else { 1 };
                        *newj = m;
                    }
                    stack.push((node_idx, m, 0, dad_idx));
                }
                0 => {
                    if let Some(lo) = node.sons[0] {
                        stack.push((node_idx, m, 1, dad_idx));
                        stack.push((lo, next_disc(m), -1, node_idx));
                    } else {
                        stack.push((node_idx, m, 1, dad_idx));
                    }
                }
                1 => {
                    let prune = if find_min {
                        j == m && node.size[m] > self.arena[*kd_minval_node_idx].size[m]
                    } else {
                        j == m && node.size[m] < self.arena[*kd_minval_node_idx].size[m]
                    };

                    if !prune {
                        if let Some(hi) = node.sons[1] {
                            stack.push((hi, next_disc(m), -1, node_idx));
                        }
                    }
                }
                _ => {}
            }
        }
        kd_data_tries
    }

    pub fn count(&self) -> i32 {
        self.item_count - self.dead_count
    }

    pub fn badness(&self) {
        let mut factor3 = 0;
        let mut max_levels = 0;
        
        fn stats<T, C>(tree: &Tree<T, C>, node_idx: Option<usize>, level: i32, factor3: &mut i32, max_levels: &mut i32) {
            if let Some(idx) = node_idx {
                let node = &tree.arena[idx];
                let has_lo = node.sons[0].is_some();
                let has_hi = node.sons[1].is_some();
                if (has_lo || has_hi) && !(has_lo && has_hi) {
                    *factor3 += 1;
                }
                if level > *max_levels {
                    *max_levels = level;
                }
                stats(tree, node.sons[0], level + 1, factor3, max_levels);
                stats(tree, node.sons[1], level + 1, factor3, max_levels);
            }
        }

        stats(self, self.root, 1, &mut factor3, &mut max_levels);

        let mut targdepth = 0.0;
        if self.item_count > 0 {
            targdepth = (self.item_count as f64).log2().floor() + 1.0;
        }

        let ratio = if targdepth > 0.0 { (max_levels as f64) / targdepth } else { 0.0 };
        let dead_pct = if self.item_count > 0 { (self.dead_count as f64 / self.item_count as f64) * 100.0 } else { 0.0 };
        let factor3_pct = if self.item_count > 0 { (factor3 as f64 / self.item_count as f64) * 100.0 } else { 0.0 };

        println!("balance ratio={:.1} (the closer to 1.0, the better), #of nodes with only one branch={} ({:.4}), max depth={}, dead={} ({:.4})",
                 ratio, factor3, factor3_pct, max_levels, self.dead_count, dead_pct);
    }
}

trait StatsHelper {
    fn num_tries_add(&mut self, val: i32);
}

impl StatsHelper for (i32, i32) {
    #[inline]
    fn num_tries_add(&mut self, val: i32) {
        self.0 += val;
    }
}

#[inline]
fn t_find_min_max_node_idx_tries<T: PartialEq + Clone, C: Coord>(tree: &Tree<T, C>, j: usize, kd_minval_node_idx: &mut usize, kd_minval_nodesdad_idx: &mut usize, dir: &mut usize, newj: &mut usize, find_min: bool) -> i32 {
    tree.find_min_max_node(j, kd_minval_node_idx, kd_minval_nodesdad_idx, dir, newj, find_min)
}

pub fn next_disc(disc: usize) -> usize {
    (disc + 1) % 6
}

#[derive(Clone, Copy, PartialEq)]
enum GenState { ThisOne, LoSon, HiSon, Done }

struct SimpleSave {
    node_idx: usize,
    disc: usize,
    state: GenState,
}

pub struct Generator<'a, T, C = i32> {
    arena: &'a Vec<Node<T, C>>,
    extent: KdBox<C>,
    stack: Vec<SimpleSave>,
}

impl<'a, T: 'a, C: Coord> Iterator for Generator<'a, T, C> {
    type Item = (&'a T, KdBox<C>);

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(top) = self.stack.last_mut() {
            let node = &self.arena[top.node_idx];
            let m = top.disc;
            let hort = m % 3;

            match top.state {
                GenState::ThisOne => {
                    top.state = GenState::LoSon;
                    if let Some(ref item) = node.item {
                        if intersect(&self.extent, &node.size) {
                            return Some((item, node.size));
                        }
                    }
                }
                GenState::LoSon => {
                    top.state = GenState::HiSon;
                    if let Some(child_idx) = node.sons[0] {
                        let mut should_push = false;
                        if m >= 3 {
                            if self.extent[hort] <= node.size[m] && self.extent[hort + 3] >= node.lo_min_bound {
                                should_push = true;
                            }
                        } else {
                            if self.extent[hort] <= node.other_bound && self.extent[hort + 3] >= node.lo_min_bound {
                                should_push = true;
                            }
                        }
                        if should_push {
                            self.stack.push(SimpleSave {
                                node_idx: child_idx,
                                disc: next_disc(m),
                                state: GenState::ThisOne,
                            });
                            continue;
                        }
                    }
                }
                GenState::HiSon => {
                    top.state = GenState::Done;
                    if let Some(child_idx) = node.sons[1] {
                        let mut should_push = false;
                        if m >= 3 {
                            if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 3] >= node.other_bound {
                                should_push = true;
                            }
                        } else {
                            if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 3] >= node.size[m] {
                                should_push = true;
                            }
                        }
                        if should_push {
                            self.stack.push(SimpleSave {
                                node_idx: child_idx,
                                disc: next_disc(m),
                                state: GenState::ThisOne,
                            });
                            continue;
                        }
                    }
                }
                GenState::Done => {
                    self.stack.pop();
                }
            }
        }
        None
    }
}

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    /// Returns an iterator over all items that intersect with the given 3D bounding box.
    pub fn start(&self, area: KdBox<C>) -> Generator<'_, T, C> {
        let mut stack = Vec::new();
        if let Some(root_idx) = self.root {
            stack.push(SimpleSave {
                node_idx: root_idx,
                disc: 0,
                state: GenState::ThisOne,
            });
        }
        Generator {
            arena: &self.arena,
            extent: area,
            stack,
        }
    }

    /// Finds the `m` nearest neighbors to the point `(x, y, z)`.
    pub fn nearest(&self, x: C, y: C, z: C, m: usize) -> Vec<Priority<T>> {
        if self.root.is_none() || m == 0 {
            return Vec::new();
        }

        let mut list = vec![Priority { dist: f64::MAX, item: None }; m];
        let xq = [x, y, z, x, y, z];
        let bp = [C::max_value(); 6];
        let bn = [C::min_value(); 6];

        self.kd_neighbor(self.root.unwrap(), &xq, m, &mut list, bp, bn);

        for p in &mut list {
            if p.dist != f64::MAX {
                p.dist = p.dist.sqrt();
            }
        }
        list
    }

    fn kd_neighbor(&self, node_idx: usize, xq: &KdBox<C>, m: usize, list: &mut [Priority<T>], bp: KdBox<C>, bn: KdBox<C>) {
        let mut stack = Vec::new();
        stack.push(Save { node_idx, disc: 0, state: State::ThisOne, bn, bp });

        while let Some(top) = stack.last_mut() {
            let node = &self.arena[top.node_idx];
            let d = top.disc;
            let p = node.size[d];
            let hort = d % 3;
            let vert = d >= 3;

            match top.state {
                State::ThisOne => {
                    top.state = State::LoSon;
                    if let Some(ref item) = node.item {
                        self.add_priority(m, list, xq, item, &node.size);
                    }
                }
                State::LoSon => {
                    top.state = State::HiSon;
                    if xq[d] <= p {
                        if let Some(child_idx) = node.sons[0] {
                            let old_bn = top.bn[hort];
                            let old_bp = top.bp[hort];
                            if vert {
                                top.bp[hort] = node.size[d];
                                top.bn[hort] = node.lo_min_bound;
                            } else {
                                top.bp[hort] = node.other_bound;
                                top.bn[hort] = node.lo_min_bound;
                            }
                            if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                                let (bn, bp) = (top.bn, top.bp);
                                stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    } else {
                        if let Some(child_idx) = node.sons[1] {
                            let old_bn = top.bn[hort];
                            let old_bp = top.bp[hort];
                            if vert {
                                top.bp[hort] = node.hi_max_bound;
                                top.bn[hort] = node.other_bound;
                            } else {
                                top.bp[hort] = node.hi_max_bound;
                                top.bn[hort] = node.size[d];
                            }
                            if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                                let (bn, bp) = (top.bn, top.bp);
                                stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    }
                }
                State::HiSon => {
                    top.state = State::Done;
                    if xq[d] <= p {
                        if let Some(child_idx) = node.sons[1] {
                            let old_bn = top.bn[hort];
                            let old_bp = top.bp[hort];
                            if vert {
                                top.bp[hort] = node.hi_max_bound;
                                top.bn[hort] = node.other_bound;
                            } else {
                                top.bp[hort] = node.hi_max_bound;
                                top.bn[hort] = node.size[d];
                            }
                            if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                                let (bn, bp) = (top.bn, top.bp);
                                stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    } else {
                        if let Some(child_idx) = node.sons[0] {
                            let old_bn = top.bn[hort];
                            let old_bp = top.bp[hort];
                            if vert {
                                top.bp[hort] = node.size[d];
                                top.bn[hort] = node.lo_min_bound;
                            } else {
                                top.bp[hort] = node.other_bound;
                                top.bn[hort] = node.lo_min_bound;
                            }
                            if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                                let (bn, bp) = (top.bn, top.bp);
                                stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    }
                }
                State::Done => {
                    stack.pop();
                }
            }
        }
    }

    fn add_priority(&self, m: usize, list: &mut [Priority<T>], xq: &KdBox<C>, item: &T, size: &KdBox<C>) {
        let d = kd_dist_sq(xq, size);
        for x in (0..m).rev() {
            if d < list[x].dist {
                if x != m - 1 {
                    list[x + 1] = list[x].clone();
                }
                list[x].dist = d;
                list[x].item = Some(item.clone());
            } else {
                break;
            }
        }
    }

    fn bounds_overlap_ball(&self, xq: &KdBox<C>, bp: &KdBox<C>, bn: &KdBox<C>, m: usize, list: &[Priority<T>]) -> bool {
        let mut sum = 0.0;
        let max_dist = list[m - 1].dist;
        for i in 0..3 {
            if xq[i] < bn[i] {
                let d = (xq[i] - bn[i]).to_f64();
                sum += d * d;
                if sum > max_dist {
                    return false;
                }
            } else if xq[i] > bp[i] {
                let d = (xq[i] - bp[i]).to_f64();
                sum += d * d;
                if sum > max_dist {
                    return false;
                }
            }
        }
        true
    }

    /// Deletes structurally an item from the tree (really delete).
    pub fn really_delete(&mut self, data: &T, old_size: &KdBox<C>) -> (Status, i32, i32) {
        let mut path = Vec::new();
        let elem_opt = self.find_item_with_path(self.root, 0, data, old_size, &mut path);
        if elem_opt.is_none() {
            return (Status::NotFound, 0, 0);
        }

        let elem_idx = elem_opt.unwrap();
        let mut stats = (0, 1);

        if Some(elem_idx) == self.root {
            self.root = self.kd_do_delete(elem_idx, 0, &mut stats);
        } else {
            let parent_idx = path[path.len() - 1];
            let j = path.len() % 6;
            let new_elem = self.kd_do_delete(elem_idx, j, &mut stats);
            let parent = &mut self.arena[parent_idx];
            if parent.sons[1] == Some(elem_idx) {
                parent.sons[1] = new_elem;
            } else {
                parent.sons[0] = new_elem;
            }
        }

        self.item_count -= 1;
        (Status::Ok, stats.0, stats.1)
    }

    fn find_item_with_path(&self, node_idx: Option<usize>, disc: usize, item: &T, size: &KdBox<C>, path: &mut Vec<usize>) -> Option<usize> {
        let idx = node_idx?;
        let node = &self.arena[idx];

        if let Some(ref node_item) = node.item {
            if *item == *node_item {
                return Some(idx);
            }
        }

        let mut val = size[disc] - node.size[disc];
        if val == C::zero() {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - node.size[ndisc];
                if val != C::zero() {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == C::zero() {
                val = C::from_i32(1);
            }
        }

        let child_idx = if val >= C::zero() { 1 } else { 0 };

        if let Some(child_node_idx) = node.sons[child_idx] {
            path.push(idx);
            return self.find_item_with_path(Some(child_node_idx), next_disc(disc), item, size, path);
        }

        None
    }
}

pub fn intersect<C: Coord>(b1: &KdBox<C>, b2: &KdBox<C>) -> bool {
    b1[RIGHT] >= b2[LEFT] &&
    b2[RIGHT] >= b1[LEFT] &&
    b1[TOP] >= b2[BOTTOM] &&
    b2[TOP] >= b1[BOTTOM] &&
    b1[CEIL] >= b2[FLOOR] &&
    b2[CEIL] >= b1[FLOOR]
}

pub fn kd_dist_sq<C: Coord>(xq: &KdBox<C>, box_val: &KdBox<C>) -> f64 {
    let mut dx = 0.0;
    let mut dy = 0.0;
    let mut dz = 0.0;

    if xq[LEFT] > box_val[RIGHT] {
        dx = (xq[LEFT] - box_val[RIGHT]).to_f64();
    } else if xq[RIGHT] < box_val[LEFT] {
        dx = (box_val[LEFT] - xq[RIGHT]).to_f64();
    }

    if xq[BOTTOM] > box_val[TOP] {
        dy = (xq[BOTTOM] - box_val[TOP]).to_f64();
    } else if xq[TOP] < box_val[BOTTOM] {
        dy = (box_val[BOTTOM] - xq[TOP]).to_f64();
    }

    if xq[FLOOR] > box_val[CEIL] {
        dz = (xq[FLOOR] - box_val[CEIL]).to_f64();
    } else if xq[CEIL] < box_val[FLOOR] {
        dz = (box_val[FLOOR] - xq[CEIL]).to_f64();
    }

    dx * dx + dy * dy + dz * dz
}

#[derive(Clone)]
pub struct Priority<T> {
    pub dist: f64,
    pub item: Option<T>,
}

#[cfg(test)]
struct Lcg {
    state: u32,
}

#[cfg(test)]
impl Lcg {
    fn next(&mut self) -> i32 {
        self.state = self.state.wrapping_mul(1664525).wrapping_add(1013904223);
        (self.state >> 16) as i32
    }

    fn next_range(&mut self, max: i32) -> i32 {
        self.next().rem_euclid(max)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const KD_BOXES: usize = 10000;
    const KD_REGIONS: usize = 100;
    const MIN_RANGE: i32 = -100000;
    const MAX_RANGE: i32 = 100000;
    const RANGE_SPAN: i32 = MAX_RANGE - MIN_RANGE + 1;
    const BOX_RANGE: i32 = 1000;

    fn rand_box(rng: &mut Lcg) -> KdBox<i32> {
        let left = rng.next_range(RANGE_SPAN) + MIN_RANGE;
        let bottom = rng.next_range(RANGE_SPAN) + MIN_RANGE;
        let floor = rng.next_range(RANGE_SPAN) + MIN_RANGE;
        let right = left + rng.next_range(BOX_RANGE);
        let top = bottom + rng.next_range(BOX_RANGE);
        let ceil = floor + rng.next_range(BOX_RANGE);
        [left, bottom, floor, right, top, ceil]
    }

    #[test]
    fn test_kd_3d() {
        let mut tree = Tree::<&str, i32>::new();
        let box1: KdBox<i32> = [0, 0, 0, 10, 10, 10];
        let box2: KdBox<i32> = [20, 20, 20, 30, 30, 30];
        let box3: KdBox<i32> = [5, 5, 5, 15, 15, 15];

        tree.insert("item1", box1);
        tree.insert("item2", box2);
        tree.insert("item3", box3);

        assert_eq!(tree.count(), 3);
        assert!(tree.is_member(&"item2", &box2));
    }

    #[test]
    fn test_nearest() {
        let mut rng = Lcg { state: 42 };
        let mut boxes = Vec::new();
        let mut tree = Tree::<usize, i32>::new();

        for i in 0..KD_BOXES {
            let b = rand_box(&mut rng);
            boxes.push(b);
            tree.insert(i, b);
        }

        for m in [1, 2, 4, 8, 16] {
            for _ in 0..50 {
                let qx = rng.next_range(RANGE_SPAN) + MIN_RANGE;
                let qy = rng.next_range(RANGE_SPAN) + MIN_RANGE;
                let qz = rng.next_range(RANGE_SPAN) + MIN_RANGE;

                let list = tree.nearest(qx, qy, qz, m);
                assert_eq!(list.len(), m);

                for i in 1..m {
                    assert!(list[i].dist >= list[i - 1].dist - 1e-9);
                }

                let mut brute: Vec<f64> = boxes.iter()
                    .map(|b| kd_dist_sq(&[qx, qy, qz, qx, qy, qz], b).sqrt())
                    .collect();
                brute.sort_by(|a, b| a.partial_cmp(b).unwrap());

                assert!(list[m - 1].dist <= brute[m - 1] + 1e-6);
            }
        }
    }
}
