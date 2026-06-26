use kdtree3d::{Tree, KdBox};
use std::time::Instant;

struct Lcg { state: u32 }
impl Lcg {
    fn next(&mut self) -> i32 {
        self.state = self.state.wrapping_mul(1664525).wrapping_add(1013904223);
        (self.state >> 16) as i32
    }
    fn next_range(&mut self, max: i32) -> i32 { self.next().rem_euclid(max) }
    fn next_u32(&mut self) -> u32 {
        let h = self.next() as u32;
        let l = self.next() as u32;
        (h << 16) | (l & 0xFFFF)
    }
}

fn main() {
    let mut tree = Tree::new();
    let mut rng = Lcg { state: 42 };

    let n_boxes = 1_000_000;
    let mut boxes = Vec::with_capacity(n_boxes);

    println!("Inserting {} boxes...", n_boxes);
    let start = Instant::now();
    for i in 0..n_boxes {
        let x1 = (rng.next_u32() % 1_000_000_000) as i32;
        let y1 = (rng.next_u32() % 1_000_000_000) as i32;
        let z1 = (rng.next_u32() % 1_000_000_000) as i32;
        let x2 = x1 + (rng.next_range(100) as i32) + 1;
        let y2 = y1 + (rng.next_range(100) as i32) + 1;
        let z2 = z1 + (rng.next_range(100) as i32) + 1;
        
        let b: KdBox = [x1, y1, z1, x2, y2, z2];
        boxes.push(b);
        tree.insert(i, b);
    }
    println!("Insertion took {:?}", start.elapsed());

    let search_area: KdBox = [0, 0, 0, 500_000_000, 500_000_000, 500_000_000];
    println!("Searching in area {:?}...", search_area);
    let start = Instant::now();
    let found_count = tree.start(search_area).count();
    println!("Found {} boxes in search area. Search took {:?}", found_count, start.elapsed());

    println!("Deleting 1000 items...");
    let start = Instant::now();
    for i in 0..1000 {
        tree.hard_delete(&i, &boxes[i]);
    }
    println!("Deletion took {:?}", start.elapsed());
}
