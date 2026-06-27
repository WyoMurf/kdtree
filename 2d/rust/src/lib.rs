use std::cmp::{min, max};

pub trait Coord: Copy + Ord + std::ops::Sub<Output = Self> + std::ops::Add<Output = Self> + Default {
    fn zero() -> Self { Self::default() }
    fn from_i32(val: i32) -> Self;
}

impl Coord for i32 {
    #[inline]
    fn from_i32(val: i32) -> Self { val }
}

impl Coord for i64 {
    #[inline]
    fn from_i32(val: i32) -> Self { val as i64 }
}

impl Coord for i128 {
    #[inline]
    fn from_i32(val: i32) -> Self { val as i128 }
}

pub type KdBox<C = i32> = [C; 4];

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

pub struct Node<T, C = i32> {
    pub item: Option<T>,
    pub size: KdBox<C>,
    pub lo_min_bound: C,
    pub hi_max_bound: C,
    pub other_bound: C,
    pub sons: [Option<Box<Node<T, C>>>; 2],
}

pub struct Tree<T, C = i32> {
    pub root: Option<Box<Node<T, C>>>,
    pub item_count: i64,
    pub dead_count: i64,
    pub extent: KdBox<C>,
    pub items_balanced: i64,
}

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    pub fn new() -> Self {
        Self {
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [C::zero(); 4],
            items_balanced: 0,
        }
    }

    pub fn insert(&mut self, item: T, size: KdBox<C>) {
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
        elem: &mut Box<Node<T, C>>,
        disc: usize,
        item: T,
        size: &KdBox<C>,
    ) -> bool {
        if let Some(ref node_item) = elem.item {
            if item == *node_item {
                return false; // Duplicate
            }
        }

        let mut val = size[disc] - elem.size[disc];
        if val == C::zero() {
            let mut ndisc = next_disc(disc);
            while ndisc != disc {
                val = size[ndisc] - elem.size[ndisc];
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
            other_bound: C::zero(),
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
        elem: Option<&'a mut Box<Node<T, C>>>,
        disc: usize,
        item: &T,
        size: &KdBox<C>,
    ) -> Option<&'a mut Node<T, C>> {
        let node = elem?;

        if let Some(ref node_item) = node.item {
            if *item == *node_item {
                return Some(node);
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
        Self::find_recursive(node.sons[child_idx].as_mut(), next_disc(disc), item, size)
    }

    pub fn hard_delete(&mut self, item: &T, size: &KdBox<C>) -> bool {
        let initial_count = self.item_count;
        self.root = Self::hard_delete_recursive(self.root.take(), 0, item, size, &mut self.item_count);
        self.item_count < initial_count
    }

    fn hard_delete_recursive(
        node_opt: Option<Box<Node<T, C>>>,
        disc: usize,
        item: &T,
        size: &KdBox<C>,
        item_count: &mut i64,
    ) -> Option<Box<Node<T, C>>> {
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
                let q_size_clone = q_size;
                
                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[1] = Self::hard_delete_recursive(node.sons[1].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count);
            } else {
                let (q_item, q_size) = Self::find_extreme(node.sons[0].as_ref().unwrap(), next_disc(disc), disc, false);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size;

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[0] = Self::hard_delete_recursive(node.sons[0].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count);
            }
            return Some(node);
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
        node.sons[child_idx] = Self::hard_delete_recursive(node.sons[child_idx].take(), next_disc(disc), item, size, item_count);
        Some(node)
    }

    fn find_extreme<'a>(
        node: &'a Node<T, C>,
        node_disc: usize,
        target_disc: usize,
        find_min: bool,
    ) -> (&'a T, KdBox<C>) {
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

    pub fn is_member(&mut self, item: &T, size: &KdBox<C>) -> bool {
        Self::find_recursive(self.root.as_mut(), 0, item, size).is_some()
    }

    pub fn count(&self) -> i64 {
        self.item_count - self.dead_count
    }

    pub fn delete(&mut self, item: &T, size: &KdBox<C>) -> bool {
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

fn bounds_update<T, C: Coord>(node: &mut Node<T, C>, disc: usize, size: &KdBox<C>) {
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

struct Save<'a, T, C> {
    node: &'a Node<T, C>,
    disc: usize,
    state: State,
}

pub struct Generator<'a, T, C = i32> {
    extent: KdBox<C>,
    stack: Vec<Save<'a, T, C>>,
}

impl<'a, T: 'a, C: Coord> Iterator for Generator<'a, T, C> {
    type Item = (&'a T, KdBox<C>);

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
                                node: &**child,
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
                                node: &**child,
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

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    pub fn start(&self, area: KdBox<C>) -> Generator<'_, T, C> {
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

pub fn intersect<C: Coord>(b1: &KdBox<C>, b2: &KdBox<C>) -> bool {
    b1[RIGHT] >= b2[LEFT] &&
    b2[RIGHT] >= b1[LEFT] &&
    b1[TOP] >= b2[BOTTOM] &&
    b2[TOP] >= b1[BOTTOM]
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
    use super::Lcg;

    macro_rules! generate_tests {
        ($t:ty, $mod_name:ident) => {
            mod $mod_name {
                use super::*;

                #[test]
                fn test_kd_tree_basic() {
                    let mut tree = Tree::<&str, $t>::new();
                    let box1: KdBox<$t> = [<$t>::from_i32(0), <$t>::from_i32(0), <$t>::from_i32(10), <$t>::from_i32(10)];
                    let box2: KdBox<$t> = [<$t>::from_i32(20), <$t>::from_i32(20), <$t>::from_i32(30), <$t>::from_i32(30)];
                    let box3: KdBox<$t> = [<$t>::from_i32(5), <$t>::from_i32(5), <$t>::from_i32(15), <$t>::from_i32(15)];

                    tree.insert("item1", box1);
                    tree.insert("item2", box2);
                    tree.insert("item3", box3);

                    assert_eq!(tree.count(), 3);
                    assert!(tree.is_member(&"item2", &box2));
                }

                #[test]
                fn test_kd_tree_hard_delete() {
                    let mut tree = Tree::<&str, $t>::new();
                    let box1: KdBox<$t> = [<$t>::from_i32(0), <$t>::from_i32(0), <$t>::from_i32(10), <$t>::from_i32(10)];
                    let box2: KdBox<$t> = [<$t>::from_i32(20), <$t>::from_i32(20), <$t>::from_i32(30), <$t>::from_i32(30)];
                    let box3: KdBox<$t> = [<$t>::from_i32(5), <$t>::from_i32(5), <$t>::from_i32(15), <$t>::from_i32(15)];

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
                    let mut tree = Tree::<String, $t>::new();
                    let mut rng = Lcg { state: 42 };
                    let mut boxes_to_delete = Vec::new();

                    for i in 0..100_000 { // Reduced to 100k to keep test suite fast
                        let x1 = rng.next_range(100000);
                        let y1 = rng.next_range(100000);
                        let x2 = x1 + rng.next_range(100) + 1;
                        let y2 = y1 + rng.next_range(100) + 1;
                        let b: KdBox<$t> = [<$t>::from_i32(x1), <$t>::from_i32(y1), <$t>::from_i32(x2), <$t>::from_i32(y2)];
                        
                        if i < 1000 {
                            boxes_to_delete.push(b);
                        }
                        tree.insert(format!("box{}", i), b);
                    }

                    assert_eq!(tree.count(), 100_000);

                    let search_area: KdBox<$t> = [<$t>::from_i32(0), <$t>::from_i32(0), <$t>::from_i32(50000), <$t>::from_i32(50000)];
                    let mut found_count = 0;
                    for _ in tree.start(search_area) {
                        found_count += 1;
                    }
                    assert!(found_count > 100);

                    for i in 0..1000 {
                        let item_name = format!("box{}", i);
                        let deleted = tree.hard_delete(&item_name, &boxes_to_delete[i]);
                        assert!(deleted, "Failed to hard delete box{}", i);
                    }

                    assert_eq!(tree.count(), 99_000);
                }
            }
        };
    }

    generate_tests!(i32, tests_i32);
    generate_tests!(i64, tests_i64);
    generate_tests!(i128, tests_i128);
}
