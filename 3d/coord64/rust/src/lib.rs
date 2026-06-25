use std::cmp::{min, max};

// Box defines a 3D bounding box [left, bottom, floor, right, top, ceil]
pub type KdBox = [i64; 6];

pub const LEFT: usize = 0;
pub const BOTTOM: usize = 1;
pub const FLOOR: usize = 2;
pub const RIGHT: usize = 3;
pub const TOP: usize = 4;
pub const CEIL: usize = 5;

pub struct Node<T> {
    pub item: Option<T>,
    pub size: KdBox,
    pub lo_min_bound: i64,
    pub hi_max_bound: i64,
    pub other_bound: i64,
    pub sons: [Option<usize>; 2],
}

/// A 3D KD-Tree implementation using arena allocation.
pub struct Tree<T> {
    pub arena: Vec<Node<T>>,
    pub free_list: Vec<usize>,
    pub root: Option<usize>,
    pub item_count: i32,
    pub dead_count: i32,
    pub extent: KdBox,
    pub delete_flip: bool,
}

#[derive(Clone, Copy, PartialEq)]
enum State { ThisOne, LoSon, HiSon, Done }

struct Save {
    node_idx: usize,
    disc: usize,
    state: State,
    bn: KdBox,
    bp: KdBox,
}

impl<T: PartialEq + Clone> Tree<T> {
    pub fn new() -> Self {
        Self {
            arena: Vec::new(),
            free_list: Vec::new(),
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [0; 6],
            delete_flip: false,
        }
    }

    pub fn with_capacity(capacity: usize) -> Self {
        Self {
            arena: Vec::with_capacity(capacity),
            free_list: Vec::new(),
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [0; 6],
            delete_flip: false,
        }
    }

    fn allocate_node(&mut self, node: Node<T>) -> usize {
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

    pub fn insert(&mut self, item: T, size: KdBox) {
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

    fn insert_recursive(&mut self, node_idx: usize, disc: usize, item: T, size: &KdBox) -> bool {
        if let Some(ref node_item) = self.arena[node_idx].item {
            if item == *node_item { return false; }
        }
        let mut val = size[disc] - self.arena[node_idx].size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - self.arena[node_idx].size[ndisc];
                if val != 0 { break; }
                ndisc = next_disc(ndisc);
            }
            if val == 0 { val = 1; }
        }
        let child_idx = if val >= 0 { 1 } else { 0 };
        if let Some(child_idx_val) = self.arena[node_idx].sons[child_idx] {
            let inserted = self.insert_recursive(child_idx_val, next_disc(disc), item, size);
            if inserted { bounds_update(&mut self.arena[node_idx], disc, size); }
            return inserted;
        }
        let vert = next_disc(disc) % 3;
        let mut new_node = Node {
            item: Some(item),
            size: *size,
            lo_min_bound: size[vert],
            hi_max_bound: size[vert + 3],
            other_bound: 0,
            sons: [None, None],
        };
        if next_disc(disc) >= 3 { new_node.other_bound = size[vert]; }
        else { new_node.other_bound = size[vert + 3]; }
        let new_idx = self.allocate_node(new_node);
        self.arena[node_idx].sons[child_idx] = Some(new_idx);
        bounds_update(&mut self.arena[node_idx], disc, size);
        true
    }

    pub fn is_member(&self, item: &T, size: &KdBox) -> bool {
        self.find_recursive(self.root, 0, item, size).is_some()
    }

    fn find_recursive(&self, node_idx_opt: Option<usize>, disc: usize, item: &T, size: &KdBox) -> Option<usize> {
        let node_idx = node_idx_opt?;
        let node = &self.arena[node_idx];
        if let Some(ref node_item) = node.item {
            if *item == *node_item { return Some(node_idx); }
        }
        let mut val = size[disc] - node.size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - node.size[ndisc];
                if val != 0 { break; }
                ndisc = next_disc(ndisc);
            }
            if val == 0 { val = 1; }
        }
        let child_idx = if val >= 0 { 1 } else { 0 };
        self.find_recursive(node.sons[child_idx], next_disc(disc), item, size)
    }

    pub fn delete(&mut self, item: &T, size: &KdBox) -> bool {
        if let Some(node_idx) = self.find_recursive(self.root, 0, item, size) {
            if self.arena[node_idx].item.is_some() {
                self.arena[node_idx].item = None;
                self.dead_count += 1;
                return true;
            }
        }
        false
    }

    pub fn rebuild(&mut self) {
        let mut items = Vec::new();
        let mut boxes = Vec::new();
        self.unload_items(self.root, &mut items, &mut boxes);
        let new_tree = Tree::build(items, boxes);
        self.arena = new_tree.arena;
        self.free_list = new_tree.free_list;
        self.root = new_tree.root;
        self.item_count = new_tree.item_count;
        self.dead_count = 0;
        self.extent = new_tree.extent;
    }

    fn unload_items(&self, node_idx_opt: Option<usize>, items: &mut Vec<T>, boxes: &mut Vec<KdBox>) {
        if let Some(node_idx) = node_idx_opt {
            let node = &self.arena[node_idx];
            if let Some(ref item) = node.item {
                items.push(item.clone());
                boxes.push(node.size);
            }
            self.unload_items(node.sons[0], items, boxes);
            self.unload_items(node.sons[1], items, boxes);
        }
    }

    pub fn build(items: Vec<T>, boxes: Vec<KdBox>) -> Self {
        let num = items.len();
        if num == 0 { return Self::new(); }
        let mut extent = [i64::MAX, i64::MAX, i64::MAX, i64::MIN, i64::MIN, i64::MIN];
        let mut mean = 0.0;
        for b in &boxes {
            for i in 0..3 {
                extent[i] = min(extent[i], b[i]);
                extent[i+3] = max(extent[i+3], b[i+3]);
            }
            mean += b[0] as f64;
        }
        mean /= num as f64;
        let mut nodes: Vec<(T, KdBox)> = items.into_iter().zip(boxes.into_iter()).collect();
        let mut tree = Self::with_capacity(num);
        tree.extent = extent;
        let root = tree.build_node_recursive(&mut nodes, 0, 1, 100000, mean);
        tree.root = root;
        tree.item_count = num as i32;
        tree
    }

    fn build_node_recursive(&mut self, nodes: &mut [(T, KdBox)], disc: usize, level: i32, max_level: i32, mean: f64) -> Option<usize> {
        let num = nodes.len();
        if num == 0 { return None; }
        let mut best_dist = f64::MAX;
        let mut best_idx = 0;
        for (i, (_, b)) in nodes.iter().enumerate() {
            let dist = (b[disc] as f64 - mean).abs();
            if dist < best_dist { best_dist = dist; best_idx = i; }
        }
        nodes.swap(0, best_idx);
        let (median_item, median_size) = nodes[0].clone();
        let median_val = median_size[disc];
        let mut lo_nodes = Vec::new();
        let mut hi_nodes = Vec::new();
        let mut eq_nodes = Vec::new();
        let mut lomean = 0.0;
        let mut himean = 0.0;
        for (item, b) in nodes.iter().skip(1) {
            let val = b[disc];
            if val < median_val { lo_nodes.push((item.clone(), *b)); lomean += b[next_disc(disc)] as f64; }
            else if val > median_val { hi_nodes.push((item.clone(), *b)); himean += b[next_disc(disc)] as f64; }
            else { eq_nodes.push((item.clone(), *b)); }
        }
        for (item, b) in eq_nodes {
            let mut val = 0;
            let mut cur_disc = next_disc(disc);
            while cur_disc != disc {
                val = b[cur_disc] - median_size[cur_disc];
                if val != 0 { break; }
                cur_disc = next_disc(cur_disc);
            }
            if val < 0 { lo_nodes.push((item, b)); lomean += b[next_disc(disc)] as f64; }
            else { hi_nodes.push((item, b)); himean += b[next_disc(disc)] as f64; }
        }
        let (lo_min, _lo_max) = get_min_max_bounds(&lo_nodes, &median_size, disc);
        let (_hi_min, hi_max) = get_min_max_bounds(&hi_nodes, &median_size, disc);
        let node = Node {
            item: Some(median_item),
            size: median_size,
            lo_min_bound: lo_min,
            hi_max_bound: hi_max,
            other_bound: if disc >= 3 { lo_min } else { hi_max },
            sons: [None, None],
        };
        let node_idx = self.allocate_node(node);
        if level < max_level {
            if !lo_nodes.is_empty() {
                let m = lomean / lo_nodes.len() as f64;
                let son = self.build_node_recursive(&mut lo_nodes, next_disc(disc), level + 1, max_level, m);
                self.arena[node_idx].sons[0] = son;
            }
            if !hi_nodes.is_empty() {
                let m = himean / hi_nodes.len() as f64;
                let son = self.build_node_recursive(&mut hi_nodes, next_disc(disc), level + 1, max_level, m);
                self.arena[node_idx].sons[1] = son;
            }
        }
        Some(node_idx)
    }

    pub fn badness(&self) {
        let mut factor3 = 0;
        let mut max_levels = 0;
        fn stats<T>(tree: &Tree<T>, node_idx_opt: Option<usize>, level: i32, factor3: &mut i32, max_levels: &mut i32) {
            if let Some(node_idx) = node_idx_opt {
                let node = &tree.arena[node_idx];
                if (node.sons[0].is_some() || node.sons[1].is_some()) && !(node.sons[0].is_some() && node.sons[1].is_some()) { *factor3 += 1; }
                if level > *max_levels { *max_levels = level; }
                stats(tree, node.sons[0], level + 1, factor3, max_levels);
                stats(tree, node.sons[1], level + 1, factor3, max_levels);
            }
        }
        stats(self, self.root, 1, &mut factor3, &mut max_levels);
        let targdepth = if self.item_count > 0 { (self.item_count as f64).log2().floor() + 1.0 } else { 0.0 };
        let ratio = if targdepth > 0.0 { max_levels as f64 / targdepth } else { 0.0 };
        
        let dead_pct = if self.item_count > 0 { (self.dead_count as f64 / self.item_count as f64) * 100.0 } else { 0.0 };
        let factor3_pct = if self.item_count > 0 { (factor3 as f64 / self.item_count as f64) * 100.0 } else { 0.0 };
        
        println!("balance ratio={} (the closer to 1.0, the better), #of nodes with only one branch={} ({}), max depth={}, dead={} ({})",
            ratio, factor3, factor3_pct, max_levels, self.dead_count, dead_pct);
    }

    pub fn hard_delete(&mut self, item: &T, size: &KdBox) -> bool {
        let initial_count = self.item_count;
        self.root = self.hard_delete_recursive(self.root, 0, item, size);
        self.item_count < initial_count
    }

    fn hard_delete_recursive(&mut self, node_idx_opt: Option<usize>, disc: usize, item: &T, size: &KdBox) -> Option<usize> {
        let node_idx = node_idx_opt?;
        let is_match = match self.arena[node_idx].item { Some(ref node_item) => node_item == item, None => false };
        if is_match {
            if self.arena[node_idx].sons[0].is_none() && self.arena[node_idx].sons[1].is_none() {
                self.item_count -= 1; self.free_node(node_idx); return None;
            }
            if self.arena[node_idx].sons[1].is_some() {
                let son_idx = self.arena[node_idx].sons[1].unwrap();
                let (q_item, q_size) = self.find_extreme(son_idx, next_disc(disc), disc, true);
                let (qi, qs) = (q_item.clone(), q_size);
                self.arena[node_idx].item = Some(qi.clone());
                self.arena[node_idx].size = qs;
                self.arena[node_idx].sons[1] = self.hard_delete_recursive(self.arena[node_idx].sons[1], next_disc(disc), &qi, &qs);
            } else {
                let son_idx = self.arena[node_idx].sons[0].unwrap();
                let (q_item, q_size) = self.find_extreme(son_idx, next_disc(disc), disc, false);
                let (qi, qs) = (q_item.clone(), q_size);
                self.arena[node_idx].item = Some(qi.clone());
                self.arena[node_idx].size = qs;
                self.arena[node_idx].sons[0] = self.hard_delete_recursive(self.arena[node_idx].sons[0], next_disc(disc), &qi, &qs);
            }
            return Some(node_idx);
        }
        let mut val = size[disc] - self.arena[node_idx].size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - self.arena[node_idx].size[ndisc];
                if val != 0 { break; }
                ndisc = next_disc(ndisc);
            }
            if val == 0 { val = 1; }
        }
        let child_idx = if val >= 0 { 1 } else { 0 };
        self.arena[node_idx].sons[child_idx] = self.hard_delete_recursive(self.arena[node_idx].sons[child_idx], next_disc(disc), item, size);
        Some(node_idx)
    }

    fn find_extreme(&self, node_idx: usize, node_disc: usize, target_disc: usize, find_min: bool) -> (T, KdBox) {
        let node = &self.arena[node_idx];
        let mut best_item = node.item.as_ref().unwrap().clone();
        let mut best_size = node.size;
        let mut search_lo = node.sons[0].is_some();
        let mut search_hi = node.sons[1].is_some();
        if node_disc == target_disc { if find_min { search_hi = false; } else { search_lo = false; } }
        if search_lo {
            let (l_item, l_size) = self.find_extreme(node.sons[0].unwrap(), next_disc(node_disc), target_disc, find_min);
            if find_min { if l_size[target_disc] < best_size[target_disc] { best_size = l_size; best_item = l_item; } }
            else { if l_size[target_disc] > best_size[target_disc] { best_size = l_size; best_item = l_item; } }
        }
        if search_hi {
            let (h_item, h_size) = self.find_extreme(node.sons[1].unwrap(), next_disc(node_disc), target_disc, find_min);
            if find_min { if h_size[target_disc] < best_size[target_disc] { best_size = h_size; best_item = h_item; } }
            else { if h_size[target_disc] > best_size[target_disc] { best_size = h_size; best_item = h_item; } }
        }
        (best_item, best_size)
    }

    pub fn really_delete(&mut self, item: &T, size: &KdBox) -> Result<(i32, i32, i32), String> {
        let path = self.find_path(self.root, 0, item, size)?;
        let elem_idx = *path.last().unwrap();
        let mut stats = (0, 1); // tries, dels

        if Some(elem_idx) == self.root {
            self.root = self.kd_do_delete(elem_idx, 0, &mut stats);
        } else {
            let parent_idx = path[path.len() - 2];
            let disc = (path.len() - 2) % 6; // parent's discriminator
            let next_disc = next_disc(disc);
            let new_node = self.kd_do_delete(elem_idx, next_disc, &mut stats);
            
            if self.arena[parent_idx].sons[1] == Some(elem_idx) {
                self.arena[parent_idx].sons[1] = new_node;
            } else {
                self.arena[parent_idx].sons[0] = new_node;
            }
        }
        
        self.item_count -= 1;
        Ok((1, stats.0, stats.1))
    }

    fn find_path(&self, node_idx_opt: Option<usize>, disc: usize, item: &T, size: &KdBox) -> Result<Vec<usize>, String> {
        let mut path = Vec::new();
        let mut curr = node_idx_opt;
        let mut curr_disc = disc;
        while let Some(idx) = curr {
            path.push(idx);
            let node = &self.arena[idx];
            if node.item.as_ref() == Some(item) {
                return Ok(path);
            }
            let mut val = size[curr_disc] - node.size[curr_disc];
            if val == 0 {
                let mut ndisc = next_disc(curr_disc);
                while ndisc != curr_disc {
                    val = size[ndisc] - node.size[ndisc];
                    if val != 0 { break; }
                    ndisc = next_disc(ndisc);
                }
                if val == 0 { val = 1; }
            }
            curr = node.sons[if val >= 0 { 1 } else { 0 }];
            curr_disc = next_disc(curr_disc);
        }
        Err("Item not found".to_string())
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
            stats.0 += self.find_min_max_node(disc, &mut q_idx, &mut q_dad_idx, &mut q_son, &mut newj, true);
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


    pub fn count(&self) -> i32 { self.item_count - self.dead_count }

    pub fn start(&self, area: KdBox) -> Generator<'_, T> {
        let mut stack = Vec::new();
        if let Some(root_idx) = self.root { stack.push(SimpleSave { node_idx: root_idx, disc: 0, state: State::ThisOne }); }
        Generator { arena: &self.arena, extent: area, stack }
    }

    pub fn nearest(&self, x: i64, y: i64, z: i64, m: usize) -> Vec<Priority<T>> {
        if self.root.is_none() || m == 0 { return Vec::new(); }
        let mut list = vec![Priority { dist: f64::MAX, item: None }; m];
        let xq = [x, y, z, x, y, z];
        let bp = [i64::MAX; 6];
        let bn = [i64::MIN; 6];
        self.kd_neighbor(self.root.unwrap(), &xq, m, &mut list, bp, bn);
        for p in &mut list { if p.dist != f64::MAX { p.dist = p.dist.sqrt(); } }
        list
    }

    fn kd_neighbor(&self, node_idx: usize, xq: &KdBox, m: usize, list: &mut [Priority<T>], bp: KdBox, bn: KdBox) {
        let mut stack = Vec::new();
        stack.push(Save { node_idx, disc: 0, state: State::ThisOne, bn, bp });
        while let Some(top) = stack.last_mut() {
            let node = &self.arena[top.node_idx];
            let d = top.disc;
            let hort = d % 3;
            let vert = d >= 3;
            match top.state {
                State::ThisOne => {
                    top.state = State::LoSon;
                    if let Some(ref item) = node.item { self.add_priority(m, list, xq, item, &node.size); }
                }
                State::LoSon => {
                    top.state = State::HiSon;
                    let side = if xq[d] <= node.size[d] { 0 } else { 1 };
                    if let Some(child_idx) = node.sons[side] {
                        let (obn, obp) = (top.bn[hort], top.bp[hort]);
                        if side == 0 {
                            if vert { top.bp[hort] = node.size[d]; top.bn[hort] = node.lo_min_bound; }
                            else { top.bp[hort] = node.other_bound; top.bn[hort] = node.lo_min_bound; }
                        } else {
                            if vert { top.bp[hort] = node.hi_max_bound; top.bn[hort] = node.other_bound; }
                            else { top.bp[hort] = node.hi_max_bound; top.bn[hort] = node.size[d]; }
                        }
                        if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                            let (bn, bp) = (top.bn, top.bp);
                            stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                            let prev = stack.len() - 2;
                            stack[prev].bn[hort] = obn; stack[prev].bp[hort] = obp;
                            continue;
                        }
                        top.bn[hort] = obn; top.bp[hort] = obp;
                    }
                }
                State::HiSon => {
                    top.state = State::Done;
                    let side = if xq[d] <= node.size[d] { 1 } else { 0 };
                    if let Some(child_idx) = node.sons[side] {
                        let (obn, obp) = (top.bn[hort], top.bp[hort]);
                        if side == 0 {
                            if vert { top.bp[hort] = node.size[d]; top.bn[hort] = node.lo_min_bound; }
                            else { top.bp[hort] = node.other_bound; top.bn[hort] = node.lo_min_bound; }
                        } else {
                            if vert { top.bp[hort] = node.hi_max_bound; top.bn[hort] = node.other_bound; }
                            else { top.bp[hort] = node.hi_max_bound; top.bn[hort] = node.size[d]; }
                        }
                        if self.bounds_overlap_ball(xq, &top.bp, &top.bn, m, list) {
                            let (bn, bp) = (top.bn, top.bp);
                            stack.push(Save { node_idx: child_idx, disc: next_disc(d), state: State::ThisOne, bn, bp });
                            let prev = stack.len() - 2;
                            stack[prev].bn[hort] = obn; stack[prev].bp[hort] = obp;
                            continue;
                        }
                        top.bn[hort] = obn; top.bp[hort] = obp;
                    }
                }
                State::Done => { stack.pop(); }
            }
        }
    }

    fn add_priority(&self, m: usize, list: &mut [Priority<T>], xq: &KdBox, item: &T, size: &KdBox) {
        let d = kd_dist_sq(xq, size);
        for x in (0..m).rev() {
            if d < list[x].dist {
                if x != m - 1 { list[x + 1] = list[x].clone(); }
                list[x].dist = d; list[x].item = Some(item.clone());
            } else { break; }
        }
    }

    fn bounds_overlap_ball(&self, xq: &KdBox, bp: &KdBox, bn: &KdBox, m: usize, list: &[Priority<T>]) -> bool {
        let mut sum = 0.0;
        let max_dist = list[m - 1].dist;
        for i in 0..3 {
            if xq[i] < bn[i] { let d = (xq[i] - bn[i]) as f64; sum += d * d; if sum > max_dist { return false; } }
            else if xq[i] > bp[i] { let d = (xq[i] - bp[i]) as f64; sum += d * d; if sum > max_dist { return false; } }
        }
        true
    }
}

fn kd_dist_sq(xq: &KdBox, box_size: &KdBox) -> f64 {
    let mut dx = 0.0; let mut dy = 0.0; let mut dz = 0.0;
    if xq[LEFT] > box_size[RIGHT] { dx = (xq[LEFT] - box_size[RIGHT]) as f64; }
    else if xq[RIGHT] < box_size[LEFT] { dx = (box_size[LEFT] - xq[RIGHT]) as f64; }
    if xq[BOTTOM] > box_size[TOP] { dy = (xq[BOTTOM] - box_size[TOP]) as f64; }
    else if xq[TOP] < box_size[BOTTOM] { dy = (box_size[BOTTOM] - xq[TOP]) as f64; }
    if xq[FLOOR] > box_size[CEIL] { dz = (xq[FLOOR] - box_size[CEIL]) as f64; }
    else if xq[CEIL] < box_size[FLOOR] { dz = (box_size[FLOOR] - xq[CEIL]) as f64; }
    dx * dx + dy * dy + dz * dz
}

pub fn next_disc(disc: usize) -> usize { (disc + 1) % 6 }

fn bounds_update<T>(node: &mut Node<T>, disc: usize, size: &KdBox) {
    let vert = disc % 3;
    node.lo_min_bound = min(node.lo_min_bound, size[vert]);
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 3]);
    if disc >= 3 { node.other_bound = min(node.other_bound, size[vert]); }
    else { node.other_bound = max(node.other_bound, size[vert + 3]); }
}

fn get_min_max_bounds<T>(nodes: &[(T, KdBox)], median_size: &KdBox, disc: usize) -> (i64, i64) {
    let vert = disc % 3;
    let mut b_min = median_size[vert];
    let mut b_max = median_size[vert + 3];
    for (_, b) in nodes {
        b_min = min(b_min, b[vert]);
        b_max = max(b_max, b[vert + 3]);
    }
    (b_min, b_max)
}

struct SimpleSave { node_idx: usize, disc: usize, state: State }

pub struct Generator<'a, T> {
    arena: &'a [Node<T>],
    extent: KdBox,
    stack: Vec<SimpleSave>,
}

impl<'a, T> Iterator for Generator<'a, T> {
    type Item = (&'a T, KdBox);
    fn next(&mut self) -> Option<Self::Item> {
        while let Some(top) = self.stack.last_mut() {
            let node = &self.arena[top.node_idx];
            let m = top.disc;
            let hort = m % 3;
            match top.state {
                State::ThisOne => {
                    top.state = State::LoSon;
                    if let Some(ref item) = node.item {
                        if intersect(&self.extent, &node.size) { return Some((item, node.size)); }
                    }
                }
                State::LoSon => {
                    top.state = State::HiSon;
                    if let Some(child_idx) = node.sons[0] {
                        let mut should_push = false;
                        if m >= 3 { if self.extent[hort] <= node.size[m] && self.extent[hort + 3] >= node.lo_min_bound { should_push = true; } }
                        else { if self.extent[hort] <= node.other_bound && self.extent[hort + 3] >= node.lo_min_bound { should_push = true; } }
                        if should_push { self.stack.push(SimpleSave { node_idx: child_idx, disc: next_disc(m), state: State::ThisOne }); continue; }
                    }
                }
                State::HiSon => {
                    top.state = State::Done;
                    if let Some(child_idx) = node.sons[1] {
                        let mut should_push = false;
                        if m >= 3 { if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 3] >= node.other_bound { should_push = true; } }
                        else { if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 3] >= node.size[m] { should_push = true; } }
                        if should_push { self.stack.push(SimpleSave { node_idx: child_idx, disc: next_disc(m), state: State::ThisOne }); continue; }
                    }
                }
                State::Done => { self.stack.pop(); }
            }
        }
        None
    }
}

pub fn intersect(b1: &KdBox, b2: &KdBox) -> bool {
    b1[RIGHT] >= b2[LEFT] && b2[RIGHT] >= b1[LEFT] &&
    b1[TOP] >= b2[BOTTOM] && b2[TOP] >= b1[BOTTOM] &&
    b1[CEIL] >= b2[FLOOR] && b2[CEIL] >= b1[FLOOR]
}

#[derive(Clone)]
pub struct Priority<T> { pub dist: f64, pub item: Option<T> }

#[cfg(test)]
struct Lcg { state: u32 }
#[cfg(test)]
impl Lcg {
    fn next(&mut self) -> i32 {
        self.state = self.state.wrapping_mul(1664525).wrapping_add(1013904223);
        (self.state >> 16) as i32
    }
    fn next_range(&mut self, max: i32) -> i32 { self.next().rem_euclid(max) }
}
#[cfg(test)]
mod tests {
    use super::*;
    const KD_BOXES: usize = 10000;
    const KD_REGIONS: usize = 100;
    const MIN_RANGE: i64 = -100000;
    const MAX_RANGE: i64 = 100000;
    const RANGE_SPAN: i32 = (MAX_RANGE - MIN_RANGE + 1) as i32;
    const BOX_RANGE: i32 = 1000;

    fn rand_box(rng: &mut Lcg) -> KdBox {
        let left = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
        let bottom = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
        let floor = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
        [left, bottom, floor, left + (rng.next_range(BOX_RANGE) as i64), bottom + (rng.next_range(BOX_RANGE) as i64), floor + (rng.next_range(BOX_RANGE) as i64)]
    }

    #[test]
    fn test_kd_tree_basic() {
        let mut tree = Tree::new();
        let b1 = [0, 0, 0, 10, 10, 10];
        let b2 = [20, 20, 20, 30, 30, 30];
        tree.insert("item1", b1);
        tree.insert("item2", b2);
        assert_eq!(tree.count(), 2);
        assert!(tree.is_member(&"item2", &b2));
    }

    #[test]
    fn test_rebuild() {
        let mut tree = Tree::new();
        let mut rng = Lcg { state: 42 };
        for i in 0..100 {
            let x = rng.next_range(1000) as i64;
            let y = rng.next_range(1000) as i64;
            let z = rng.next_range(1000) as i64;
            tree.insert(i, [x, y, z, x + 10, y + 10, z + 10]);
        }
        tree.badness();
        tree.rebuild();
        assert_eq!(tree.count(), 100);
        tree.badness();
    }

    #[test]
    fn test_soft_delete() {
        let mut rng = Lcg { state: 42 };
        let mut boxes = Vec::new();
        let mut tree = Tree::new();
        for i in 0..KD_BOXES {
            let b = rand_box(&mut rng);
            boxes.push(b);
            tree.insert(i, b);
        }
        for i in 0..KD_REGIONS {
            let region = rand_box(&mut rng);
            let mut n = 0;
            for (item, _) in tree.start(region) {
                assert!(intersect(&region, &boxes[*item]));
                n += 1;
            }
            let mut expected = 0;
            for b in &boxes { if intersect(&region, b) { expected += 1; } }
            assert_eq!(n, expected, "Region {} mismatch", i);
        }
        for i in (0..KD_BOXES).rev() { assert!(tree.delete(&i, &boxes[i])); }
        assert_eq!(tree.count(), 0);
    }

    #[test]
    fn test_hard_delete() {
        let mut rng = Lcg { state: 42 };
        let mut boxes = Vec::new();
        let mut tree = Tree::new();
        for i in 0..KD_BOXES {
            let b = rand_box(&mut rng);
            boxes.push(b);
            tree.insert(i, b);
        }
        for i in (0..KD_BOXES).rev() {
            if !tree.hard_delete(&i, &boxes[i]) {
                panic!("Failed to hard delete item {}", i);
            }
        }
        assert_eq!(tree.count(), 0);
    }

    #[test]
    fn test_really_delete() {
        let mut rng = Lcg { state: 42 };
        let mut boxes = Vec::new();
        let mut tree = Tree::new();
        for i in 0..KD_BOXES {
            let b = rand_box(&mut rng);
            boxes.push(b);
            tree.insert(i, b);
        }
        for i in (0..KD_BOXES).rev() {
            let res = tree.really_delete(&i, &boxes[i]);
            assert!(res.is_ok(), "Failed to really delete item {}", i);
        }
        assert_eq!(tree.count(), 0);
    }

    #[test]
    fn test_badness() {
        let mut rng = Lcg { state: 42 };
        let mut tree = Tree::new();
        for i in 0..1000 {
            let b = rand_box(&mut rng);
            tree.insert(i, b);
        }
        tree.badness();
    }


    #[test]
    fn test_nearest() {
        let mut rng = Lcg { state: 42 };
        let mut boxes = Vec::new();
        let mut tree = Tree::new();
        for i in 0..KD_BOXES {
            let b = rand_box(&mut rng);
            boxes.push(b);
            tree.insert(i, b);
        }
        for m in [1, 2, 4, 8, 16] {
            for _ in 0..50 {
                let qx = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
                let qy = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
                let qz = (rng.next_range(RANGE_SPAN) as i64) + MIN_RANGE;
                let list = tree.nearest(qx, qy, qz, m);
                assert_eq!(list.len(), m);
                for i in 1..m { assert!(list[i].dist >= list[i-1].dist - 1e-9); }
                let mut brute: Vec<f64> = boxes.iter().map(|b| kd_dist_sq(&[qx, qy, qz, qx, qy, qz], b).sqrt()).collect();
                brute.sort_by(|a, b| a.partial_cmp(b).unwrap());
                assert!(list[m-1].dist <= brute[m-1] + 1e-6);
            }
        }
    }
}
