use std::cmp::{min, max};

pub type KdBox = [i64; 4];

pub const LEFT: usize = 0;
pub const BOTTOM: usize = 1;
pub const RIGHT: usize = 2;
pub const TOP: usize = 3;

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Status {
    Ok = 1,
    NoMore = 2,
    NotImpl = -3,
    NotFound = -4,
}

pub struct Node<T> {
    pub item: Option<T>,
    pub size: KdBox,
    pub lo_min_bound: i64,
    pub hi_max_bound: i64,
    pub other_bound: i64,
    pub sons: [Option<Box<Node<T>>>; 2],
}

pub struct Tree<T> {
    pub root: Option<Box<Node<T>>>,
    pub item_count: i64,
    pub dead_count: i64,
    pub extent: KdBox,
    pub items_balanced: i64,
}

impl<T: PartialEq + Clone> Tree<T> {
    pub fn new() -> Self {
        Self {
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [0; 4],
            items_balanced: 0,
        }
    }

    pub fn insert(&mut self, item: T, size: KdBox) {
        if self.root.is_none() {
            self.root = Some(Box::new(Node {
                item: Some(item),
                size,
                lo_min_bound: size[0],
                hi_max_bound: size[2],
                other_bound: size[0],
                sons: [None, None],
            }));
            self.extent = size;
            self.item_count = 1;
            return;
        }

        if Self::insert_recursive(self.root.as_mut().unwrap(), 0, item, &size) {
            self.item_count += 1;
            self.extent[LEFT] = min(self.extent[LEFT], size[LEFT]);
            self.extent[RIGHT] = max(self.extent[RIGHT], size[RIGHT]);
            self.extent[BOTTOM] = min(self.extent[BOTTOM], size[BOTTOM]);
            self.extent[TOP] = max(self.extent[TOP], size[TOP]);
        }
    }



    fn insert_recursive(
        elem: &mut Box<Node<T>>,
        disc: usize,
        item: T,
        size: &KdBox,
    ) -> bool {
        if let Some(ref node_item) = elem.item {
            if item == *node_item {
                return false; // Duplicate
            }
        }

        let mut val = size[disc] - elem.size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - elem.size[ndisc];
                if val != 0 {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == 0 {
                val = 1;
            }
        }

        let child_idx = if val >= 0 { 1 } else { 0 };

        if let Some(ref mut child) = elem.sons[child_idx] {
            let inserted = Self::insert_recursive(child, next_disc(disc), item, size);
            if inserted {
                bounds_update(elem, disc, size);
            }
            return inserted;
        }

        let vert = next_disc(disc) & 0x01;
        let mut new_node = Node {
            item: Some(item),
            size: *size,
            lo_min_bound: size[vert],
            hi_max_bound: size[vert + 2],
            other_bound: 0,
            sons: [None, None],
        };

        if (next_disc(disc) & 0x2) != 0 {
            new_node.other_bound = size[vert];
        } else {
            new_node.other_bound = size[vert + 2];
        }

        elem.sons[child_idx] = Some(Box::new(new_node));
        bounds_update(elem, disc, size);
        true
    }

    fn find_recursive<'a>(
        elem: Option<&'a mut Box<Node<T>>>,
        disc: usize,
        item: &T,
        size: &KdBox,
    ) -> Option<&'a mut Node<T>> {
        let node = elem?;

        if let Some(ref node_item) = node.item {
            if *item == *node_item {
                return Some(node);
            }
        }

        let mut val = size[disc] - node.size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - node.size[ndisc];
                if val != 0 {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == 0 {
                val = 1;
            }
        }

        let child_idx = if val >= 0 { 1 } else { 0 };
        Self::find_recursive(node.sons[child_idx].as_mut(), next_disc(disc), item, size)
    }


    pub fn hard_delete(&mut self, item: &T, size: &KdBox) -> bool {
        let initial_count = self.item_count;
        self.root = Self::hard_delete_recursive(self.root.take(), 0, item, size, &mut self.item_count);
        self.item_count < initial_count
    }

    fn hard_delete_recursive(
        node_opt: Option<Box<Node<T>>>,
        disc: usize,
        item: &T,
        size: &KdBox,
        item_count: &mut i64,
    ) -> Option<Box<Node<T>>> {
        let mut node = node_opt?;

        let is_match = match node.item {
            Some(ref node_item) => node_item == item,
            None => false,
        };

        if is_match {
            if node.sons[0].is_none() && node.sons[1].is_none() {
                *item_count -= 1;
                return None;
            }

            if node.sons[1].is_some() {
                let (q_item, q_size) = Self::find_extreme(node.sons[1].as_ref().unwrap(), next_disc(disc), disc, true);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size.clone();
                
                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[1] = Self::hard_delete_recursive(node.sons[1].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count);
            } else {
                let (q_item, q_size) = Self::find_extreme(node.sons[0].as_ref().unwrap(), next_disc(disc), disc, false);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size.clone();

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[0] = Self::hard_delete_recursive(node.sons[0].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count);
            }
            return Some(node);
        }

        let mut val = size[disc] - node.size[disc];
        if val == 0 {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - node.size[ndisc];
                if val != 0 {
                    break;
                }
                ndisc = next_disc(ndisc);
            }
            if val == 0 {
                val = 1;
            }
        }

        let child_idx = if val >= 0 { 1 } else { 0 };
        node.sons[child_idx] = Self::hard_delete_recursive(node.sons[child_idx].take(), next_disc(disc), item, size, item_count);
        Some(node)
    }

    fn find_extreme<'a>(
        node: &'a Node<T>,
        node_disc: usize,
        target_disc: usize,
        find_min: bool,
    ) -> (&'a T, KdBox) {
        let mut best_item = node.item.as_ref().unwrap();
        let mut best_size = node.size;

        let mut search_loson = node.sons[0].is_some();
        let mut search_hison = node.sons[1].is_some();

        if node_disc == target_disc {
            if find_min {
                search_hison = false;
            } else {
                search_loson = false;
            }
        }

        if search_loson {
            let (l_item, l_size) = Self::find_extreme(node.sons[0].as_ref().unwrap(), next_disc(node_disc), target_disc, find_min);
            if find_min {
                if l_size[target_disc] < best_size[target_disc] {
                    best_size = l_size; best_item = l_item;
                }
            } else {
                if l_size[target_disc] > best_size[target_disc] {
                    best_size = l_size; best_item = l_item;
                }
            }
        }

        if search_hison {
            let (h_item, h_size) = Self::find_extreme(node.sons[1].as_ref().unwrap(), next_disc(node_disc), target_disc, find_min);
            if find_min {
                if h_size[target_disc] < best_size[target_disc] {
                    best_size = h_size; best_item = h_item;
                }
            } else {
                if h_size[target_disc] > best_size[target_disc] {
                    best_size = h_size; best_item = h_item;
                }
            }
        }

        (best_item, best_size)
    }

    pub fn is_member(&mut self, item: &T, size: &KdBox) -> bool {
        Self::find_recursive(self.root.as_mut(), 0, item, size).is_some()
    }

    pub fn count(&self) -> i64 {
        self.item_count - self.dead_count
    }

    pub fn delete(&mut self, item: &T, size: &KdBox) -> bool {
        if let Some(node) = Self::find_recursive(self.root.as_mut(), 0, item, size) {
            if node.item.is_some() {
                node.item = None;
                self.dead_count += 1;
                return true;
            }
        }
        false
    }

    }


pub fn next_disc(disc: usize) -> usize {
    (disc + 1) % 4
}

fn bounds_update<T>(node: &mut Node<T>, disc: usize, size: &KdBox) {
    let vert = disc & 0x01;
    node.lo_min_bound = min(node.lo_min_bound, size[vert]);
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 2]);
    if (disc & 0x2) != 0 {
        node.other_bound = min(node.other_bound, size[vert]);
    } else {
        node.other_bound = max(node.other_bound, size[vert + 2]);
    }

    }


#[derive(Clone, Copy, PartialEq)]
enum State {
    ThisOne,
    LoSon,
    HiSon,
    Done,
}

struct Save<'a, T> {
    node: &'a Node<T>,
    disc: usize,
    state: State,
}

pub struct Generator<'a, T> {
    extent: KdBox,
    stack: Vec<Save<'a, T>>,
}

impl<'a, T> Iterator for Generator<'a, T> {
    type Item = (&'a T, KdBox);

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(top) = self.stack.last_mut() {
            let node = top.node;
            let m = top.disc;
            let hort = m & 0x01;

            match top.state {
                State::ThisOne => {
                    top.state = State::LoSon;
                    if let Some(ref item) = node.item {
                        if intersect(&self.extent, &node.size) {
                            return Some((item, node.size));
                        }
                    }
                }
                State::LoSon => {
                    top.state = State::HiSon;
                    if let Some(ref child) = node.sons[0] {
                        let mut should_push = false;
                        if (m & 0x02) != 0 {
                            if self.extent[hort] <= node.size[m] && self.extent[hort + 2] >= node.lo_min_bound {
                                should_push = true;
                            }
                        } else {
                            if self.extent[hort] <= node.other_bound && self.extent[hort + 2] >= node.lo_min_bound {
                                should_push = true;
                            }
                        }
                        if should_push {
                            self.stack.push(Save {
                                node: child.as_ref(),
                                disc: next_disc(m),
                                state: State::ThisOne,
                            });
                            continue;
                        }
                    }
                }
                State::HiSon => {
                    top.state = State::Done;
                    if let Some(ref child) = node.sons[1] {
                        let mut should_push = false;
                        if (m & 0x02) != 0 {
                            if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 2] >= node.other_bound {
                                should_push = true;
                            }
                        } else {
                            if self.extent[hort] <= node.hi_max_bound && self.extent[hort + 2] >= node.size[m] {
                                should_push = true;
                            }
                        }
                        if should_push {
                            self.stack.push(Save {
                                node: child.as_ref(),
                                disc: next_disc(m),
                                state: State::ThisOne,
                            });
                            continue;
                        }
                    }
                }
                State::Done => {
                    self.stack.pop();
                }
            }
        }
        None
    }

    }


impl<T: PartialEq + Clone> Tree<T> {
    pub fn start(&self, area: KdBox) -> Generator<'_, T> {
        let mut stack = Vec::new();
        if let Some(ref root) = self.root {
            stack.push(Save {
                node: root.as_ref(),
                disc: 0,
                state: State::ThisOne,
            });
        }
        Generator {
            extent: area,
            stack,
        }
    }

    }


pub fn intersect(b1: &KdBox, b2: &KdBox) -> bool {
    b1[RIGHT] >= b2[LEFT] &&
    b2[RIGHT] >= b1[LEFT] &&
    b1[TOP] >= b2[BOTTOM] &&
    b2[TOP] >= b1[BOTTOM]
}



#[cfg(test)]
struct Lcg {
    state: u64,
}

#[cfg(test)]
impl Lcg {
    fn next(&mut self) -> i64 {
        self.state = self.state.wrapping_mul(6364136223846793005).wrapping_add(1);
        (self.state >> 32) as i64
    }

    fn next_range(&mut self, max: i64) -> i64 {
        self.next().rem_euclid(max) as i64
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::Lcg;

    #[test]
    fn test_kd_tree_basic() {
        let mut tree = Tree::new();
        let box1: KdBox = [0, 0, 10, 10];
        let box2: KdBox = [20, 20, 30, 30];
        let box3: KdBox = [5, 5, 15, 15];

        tree.insert("item1", box1);
        tree.insert("item2", box2);
        tree.insert("item3", box3);

        assert_eq!(tree.count(), 3);
        assert!(tree.is_member(&"item2", &box2));
    }

    #[test]
    fn test_kd_tree_hard_delete() {
        let mut tree = Tree::new();
        let box1: KdBox = [0, 0, 10, 10];
        let box2: KdBox = [20, 20, 30, 30];
        let box3: KdBox = [5, 5, 15, 15];

        tree.insert("item1", box1);
        tree.insert("item2", box2);
        tree.insert("item3", box3);

        assert!(tree.hard_delete(&"item1", &box1));
        assert_eq!(tree.count(), 2);
        assert!(tree.is_member(&"item2", &box2));
        assert!(tree.is_member(&"item3", &box3));
    }

    #[test]
    fn test_million_boxes() {
        let mut tree = Tree::new();
        let mut rng = Lcg { state: 42 };
        let mut boxes_to_delete = Vec::new();

        for i in 0..1_000_000 {
            let x1 = rng.next_range(10000000000);
            let y1 = rng.next_range(10000000000);
            let x2 = x1 + rng.next_range(100) + 1;
            let y2 = y1 + rng.next_range(100) + 1;
            let b: KdBox = [x1, y1, x2, y2];
            
            if i < 1000 {
                boxes_to_delete.push(b);
            }
            tree.insert(format!("box{}", i), b);
        }

        assert_eq!(tree.count(), 1_000_000);

        let search_area: KdBox = [0, 0, 5000000000, 5000000000];
        let mut found_count = 0;
        for _ in tree.start(search_area) {
            found_count += 1;
        }
        println!("Found {} boxes in the 0-50000 search area", found_count);

        for i in 0..1000 {
            let item_name = format!("box{}", i);
            let deleted = tree.hard_delete(&item_name, &boxes_to_delete[i]);
            assert!(deleted, "Failed to hard delete box{}", i);
        }

        assert_eq!(tree.count(), 999_000);
    }
}
