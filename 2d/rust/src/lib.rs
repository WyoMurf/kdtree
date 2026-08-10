pub trait Coord: Copy + PartialOrd + std::ops::Sub<Output = Self> + std::ops::Add<Output = Self> + Default {
    fn zero() -> Self { Self::default() }
    fn from_i32(val: i32) -> Self;
    fn min_value() -> Self;
    fn max_value() -> Self;
    fn to_f64(self) -> f64;
    fn write_to<W: std::io::Write>(self, writer: &mut W) -> std::io::Result<()>;
    fn read_from<R: std::io::Read>(reader: &mut R) -> std::io::Result<Self>;
    #[inline]
    fn cmin(self, other: Self) -> Self { if self < other { self } else { other } }
    #[inline]
    fn cmax(self, other: Self) -> Self { if self > other { self } else { other } }
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
    #[inline]
    fn write_to<W: std::io::Write>(self, writer: &mut W) -> std::io::Result<()> {
        writer.write_all(&self.to_le_bytes())
    }
    #[inline]
    fn read_from<R: std::io::Read>(reader: &mut R) -> std::io::Result<Self> {
        let mut buf = [0u8; 4];
        reader.read_exact(&mut buf)?;
        Ok(i32::from_le_bytes(buf))
    }
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
    #[inline]
    fn write_to<W: std::io::Write>(self, writer: &mut W) -> std::io::Result<()> {
        writer.write_all(&self.to_le_bytes())
    }
    #[inline]
    fn read_from<R: std::io::Read>(reader: &mut R) -> std::io::Result<Self> {
        let mut buf = [0u8; 8];
        reader.read_exact(&mut buf)?;
        Ok(i64::from_le_bytes(buf))
    }
}

impl Coord for i128 {
    #[inline]
    fn from_i32(val: i32) -> Self { val as i128 }
    #[inline]
    fn min_value() -> Self { i128::MIN }
    #[inline]
    fn max_value() -> Self { i128::MAX }
    #[inline]
    fn to_f64(self) -> f64 { self as f64 }
    #[inline]
    fn write_to<W: std::io::Write>(self, writer: &mut W) -> std::io::Result<()> {
        writer.write_all(&self.to_le_bytes())
    }
    #[inline]
    fn read_from<R: std::io::Read>(reader: &mut R) -> std::io::Result<Self> {
        let mut buf = [0u8; 16];
        reader.read_exact(&mut buf)?;
        Ok(i128::from_le_bytes(buf))
    }
}

impl Coord for f64 {
    #[inline]
    fn from_i32(val: i32) -> Self { val as f64 }
    #[inline]
    fn min_value() -> Self { f64::NEG_INFINITY }
    #[inline]
    fn max_value() -> Self { f64::INFINITY }
    #[inline]
    fn to_f64(self) -> f64 { self }
    #[inline]
    fn write_to<W: std::io::Write>(self, writer: &mut W) -> std::io::Result<()> {
        writer.write_all(&self.to_le_bytes())
    }
    #[inline]
    fn read_from<R: std::io::Read>(reader: &mut R) -> std::io::Result<Self> {
        let mut buf = [0u8; 8];
        reader.read_exact(&mut buf)?;
        Ok(f64::from_le_bytes(buf))
    }
}

/// Mean radius of the Earth in kilometers, suitable for [`haversine_distance`].
pub const EARTH_RADIUS_KM: f64 = 6371.0;

/// WGS-84 semi-major axis of the Earth in meters, suitable for [`vincenty_distance`].
pub const EARTH_SEMI_MAJOR_AXIS_M: f64 = 6378137.0;

/// WGS-84 flattening of the Earth, suitable for [`vincenty_distance`].
pub const EARTH_FLATTENING: f64 = 1.0 / 298.257223563;

/// Great-circle distance between two lat/lon points (in degrees) on a perfect sphere of the
/// given `radius`, using the haversine formula. This is fast and approximate: it assumes the
/// body is a perfect sphere, so on Earth it can be off by up to ~0.5% compared to an ellipsoidal
/// model such as [`vincenty_distance`].
pub fn haversine_distance(lat1: f64, lon1: f64, lat2: f64, lon2: f64, radius: f64) -> f64 {
    let to_rad = std::f64::consts::PI / 180.0;
    let phi1 = lat1 * to_rad;
    let phi2 = lat2 * to_rad;
    let dphi = (lat2 - lat1) * to_rad;
    let dlambda = (lon2 - lon1) * to_rad;
    let a = (dphi / 2.0).sin().powi(2) + phi1.cos() * phi2.cos() * (dlambda / 2.0).sin().powi(2);
    let c = 2.0 * a.sqrt().atan2((1.0 - a).sqrt());
    radius * c
}

/// Distance between two lat/lon points (in degrees) on an oblate spheroid defined by
/// `semi_major_axis` and `flattening`, computed via Vincenty's iterative inverse formula. This is
/// slower than [`haversine_distance`] but exact for the chosen ellipsoid model. Pass
/// [`EARTH_SEMI_MAJOR_AXIS_M`] and [`EARTH_FLATTENING`] to model the WGS-84 Earth specifically.
///
/// Known limitation: Vincenty's formula can fail to fully converge for nearly-antipodal points.
/// This implementation caps iteration at 200 rounds so it still returns a finite best-effort
/// value instead of hanging or panicking; the result's accuracy is not guaranteed in that regime.
pub fn vincenty_distance(lat1: f64, lon1: f64, lat2: f64, lon2: f64, semi_major_axis: f64, flattening: f64) -> f64 {
    let to_rad = std::f64::consts::PI / 180.0;
    let a = semi_major_axis;
    let f = flattening;
    let b = a * (1.0 - f);
    let (lat1_r, lat2_r) = (lat1 * to_rad, lat2 * to_rad);
    let l = (lon2 - lon1) * to_rad;
    let u1 = ((1.0 - f) * lat1_r.tan()).atan();
    let u2 = ((1.0 - f) * lat2_r.tan()).atan();
    let (sin_u1, cos_u1) = (u1.sin(), u1.cos());
    let (sin_u2, cos_u2) = (u2.sin(), u2.cos());

    let mut lambda = l;
    let mut sin_sigma = 0.0;
    let mut cos_sigma = 1.0;
    let mut sigma = 0.0;
    let mut cos_sq_alpha = 1.0;
    let mut cos2_sigma_m = 0.0;

    for _ in 0..200 {
        let sin_lambda = lambda.sin();
        let cos_lambda = lambda.cos();
        sin_sigma = ((cos_u2 * sin_lambda).powi(2)
            + (cos_u1 * sin_u2 - sin_u1 * cos_u2 * cos_lambda).powi(2))
            .sqrt();
        if sin_sigma == 0.0 {
            // Coincident points.
            return 0.0;
        }
        cos_sigma = sin_u1 * sin_u2 + cos_u1 * cos_u2 * cos_lambda;
        sigma = sin_sigma.atan2(cos_sigma);
        let sin_alpha = cos_u1 * cos_u2 * sin_lambda / sin_sigma;
        cos_sq_alpha = 1.0 - sin_alpha * sin_alpha;
        cos2_sigma_m = if cos_sq_alpha != 0.0 {
            cos_sigma - 2.0 * sin_u1 * sin_u2 / cos_sq_alpha
        } else {
            0.0
        };
        let c = f / 16.0 * cos_sq_alpha * (4.0 + f * (4.0 - 3.0 * cos_sq_alpha));
        let lambda_prev = lambda;
        lambda = l
            + (1.0 - c)
                * f
                * sin_alpha
                * (sigma
                    + c * sin_sigma
                        * (cos2_sigma_m + c * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m)));
        if (lambda - lambda_prev).abs() < 1e-12 {
            break;
        }
    }

    let u_sq = cos_sq_alpha * (a * a - b * b) / (b * b);
    let big_a = 1.0 + u_sq / 16384.0 * (4096.0 + u_sq * (-768.0 + u_sq * (320.0 - 175.0 * u_sq)));
    let big_b = u_sq / 1024.0 * (256.0 + u_sq * (-128.0 + u_sq * (74.0 - 47.0 * u_sq)));
    let delta_sigma = big_b
        * sin_sigma
        * (cos2_sigma_m
            + big_b / 4.0
                * (cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m)
                    - big_b / 6.0
                        * cos2_sigma_m
                        * (-3.0 + 4.0 * sin_sigma * sin_sigma)
                        * (-3.0 + 4.0 * cos2_sigma_m * cos2_sigma_m)));
    b * big_a * (sigma - delta_sigma)
}

/// Converts a degrees/minutes/seconds angle into decimal degrees. `deg` and `min` are
/// non-negative magnitudes and `sec` is always `f64`; the separate `sign` parameter (`+1` or
/// `-1`) carries the sign of the overall angle. This is necessary to correctly represent angles
/// between -1 and 0 degrees (e.g. a declination of -0 deg 15 min), which cannot be represented by
/// a signed `deg` field alone.
pub fn dms_to_degrees<C: Coord>(sign: i32, deg: C, min: C, sec: f64) -> f64 {
    (sign as f64) * (deg.to_f64() + min.to_f64() / 60.0 + sec / 3600.0)
}

/// Converts decimal degrees into a degrees/minutes/seconds angle. Returns `(sign, deg, min, sec)`
/// where `sign` is `+1` or `-1`, `deg` and `min` are non-negative magnitudes, and `sec` is always
/// `f64`. See [`dms_to_degrees`] for why the sign is carried separately from the magnitude.
pub fn degrees_to_dms<C: Coord>(degrees: f64) -> (i32, C, C, f64) {
    let sign = if degrees < 0.0 { -1 } else { 1 };
    let a = degrees.abs();
    let deg_f = a.floor();
    let rem_min = (a - deg_f) * 60.0;
    let min_f = rem_min.floor();
    let sec = (rem_min - min_f) * 60.0;
    (sign, C::from_i32(deg_f as i32), C::from_i32(min_f as i32), sec)
}

/// Interleaves the bits of two 32-bit integers into a 64-bit Morton (Z-order curve) code -- the
/// standard way to build a HEALPix NESTED pixel index from a face's local (i, j) grid
/// coordinates.
fn interleave_bits(x: u32, y: u32) -> u64 {
    let mut res: u64 = 0;
    for i in 0..32 {
        res |= (((x & (1u32 << i)) as u64) << i) | (((y & (1u32 << i)) as u64) << (i + 1));
    }
    res
}

/// Converts an equatorial-style (ra, dec) or geographic (lon, lat) pair, in degrees, into a
/// HEALPix NESTED-scheme pixel index at the given resolution `level` (nside = 2^level;
/// 12*nside^2 cells total over the whole sphere -- level 3 is 768 cells, a common "roughly a
/// thousand tiles" choice; valid levels are 0..29). The two angle arguments are mathematically
/// interchangeable -- this is the same equatorial-coordinate projection either way -- so pass
/// right ascension/declination for astronomical data, or longitude/latitude for terrestrial
/// data. `ra_or_lon_deg` is normalized internally, so it may be given in either the conventional
/// `[0, 360)` astronomical range or the conventional `[-180, 180)` geographic range; callers
/// don't need to pre-normalize longitude before calling.
pub fn healpix_nested_index(ra_or_lon_deg: f64, dec_or_lat_deg: f64, level: u32) -> u64 {
    let to_rad = std::f64::consts::PI / 180.0;
    let half_pi = std::f64::consts::PI / 2.0;

    let mut lon = ra_or_lon_deg % 360.0;
    if lon < 0.0 {
        lon += 360.0;
    }

    let phi = lon * to_rad;
    let z = (dec_or_lat_deg * to_rad).sin();

    let nside: u64 = 1u64 << level;
    let face_pixels = nside * nside;

    let xc: f64;
    let yc: f64;
    if z.abs() <= 2.0 / 3.0 {
        xc = phi;
        yc = 1.5 * z;
    } else {
        // Polar caps.
        let sgn = if z >= 0.0 { 1.0 } else { -1.0 };
        let sigma = (3.0 * (1.0 - z.abs())).sqrt();
        yc = sgn * (2.0 - sigma);

        // Find which of the 4 polar facets we are in.
        let mut facet = (phi / half_pi) as i32;
        if facet < 0 {
            facet = 0;
        }
        if facet > 3 {
            facet = 3;
        }
        let phi_c = (facet as f64 + 0.5) * half_pi;
        xc = phi_c + (phi - phi_c) * sigma;
    }

    // Project to oblique grid coordinates (scaled by pi/2).
    let pa = xc / half_pi;
    let pb = yc / half_pi;

    let u = pa + pb / 2.0;
    let v = pa - pb / 2.0;

    let ku = u.floor();
    let kv = v.floor();
    let u_frac = u - ku;
    let v_frac = v - kv;

    // Translate (ku, kv) oblique grid coordinate to base face ID (0..11).
    let ku_i = ku as i32;
    let kv_i = kv as i32;

    let face: u64 = if ku_i >= 0 && kv_i >= 0 {
        if ku_i < 4 && kv_i < 4 {
            ((4 - kv_i + ku_i % 4) % 4 + 4) as u64 // Equatorial
        } else {
            (ku_i % 4) as u64 // North cap
        }
    } else if ku_i < 0 && kv_i < 0 {
        let mut ku_mod = ku_i % 4;
        if ku_mod < 0 {
            ku_mod += 4;
        }
        (8 + ku_mod) as u64 // South cap
    } else {
        0
    };

    // Grid coordinates inside the face.
    let mut i = (u_frac * nside as f64) as u32;
    let mut j = (v_frac * nside as f64) as u32;
    if i as u64 >= nside {
        i = (nside - 1) as u32;
    }
    if j as u64 >= nside {
        j = (nside - 1) as u32;
    }

    // Interleave bits for NESTED scheme.
    let morton = interleave_bits(i, j);
    face * face_pixels + morton
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
    pub delete_flip: bool,
}

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    pub fn new() -> Self {
        Self {
            root: None,
            item_count: 0,
            dead_count: 0,
            extent: [C::zero(); 4],
            items_balanced: 0,
            delete_flip: false,
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
            self.extent[LEFT] = self.extent[LEFT].cmin(size[LEFT]);
            self.extent[RIGHT] = self.extent[RIGHT].cmax(size[RIGHT]);
            self.extent[BOTTOM] = self.extent[BOTTOM].cmin(size[BOTTOM]);
            self.extent[TOP] = self.extent[TOP].cmax(size[TOP]);
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

    /// Read-only membership check used to disambiguate an exact tie in
    /// `find_recursive`/`hard_delete_recursive`/`really_delete_recursive` without
    /// needing two overlapping `&mut` borrows of the same node's two children --
    /// Rust's borrow checker can't prove `sons[0]` and `sons[1]` are disjoint across
    /// two sequential recursive calls each returning a reference tied to the same
    /// lifetime, so the mutable search below only ever descends into one side once
    /// this has determined which side actually holds the item.
    fn contains_recursive(node: Option<&Node<T, C>>, disc: usize, item: &T, size: &KdBox<C>) -> bool {
        let node = match node {
            Some(n) => n,
            None => return false,
        };

        if let Some(ref node_item) = node.item {
            if *item == *node_item {
                return true;
            }
        }

        let val = size[disc] - node.size[disc];
        let next = next_disc(disc);

        if val == C::zero() {
            return Self::contains_recursive(node.sons[0].as_deref(), next, item, size)
                || Self::contains_recursive(node.sons[1].as_deref(), next, item, size);
        }

        let child_idx = if val > C::zero() { 1 } else { 0 };
        Self::contains_recursive(node.sons[child_idx].as_deref(), next, item, size)
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

        let val = size[disc] - node.size[disc];
        let next = next_disc(disc);

        let child_idx = if val == C::zero() {
            // Exact tie on this node's split axis: the item may legitimately live in
            // either subtree. We can't resolve this the way `insert` does (comparing
            // this node's *other* axes), because delete's promotion step can later
            // swap a different item into this position, changing those other-axis
            // values without changing which subtree the original item was placed in.
            // Check (read-only) which side actually holds it rather than guessing.
            if Self::contains_recursive(node.sons[0].as_deref(), next, item, size) { 0 } else { 1 }
        } else if val > C::zero() { 1 } else { 0 };

        Self::find_recursive(node.sons[child_idx].as_mut(), next, item, size)
    }

    pub fn hard_delete(&mut self, item: &T, size: &KdBox<C>) -> bool {
        let initial_count = self.item_count;
        self.root = Self::hard_delete_recursive(self.root.take(), 0, item, size, &mut self.item_count, &mut self.delete_flip);
        self.item_count < initial_count
    }

    fn hard_delete_recursive(
        node_opt: Option<Box<Node<T, C>>>,
        disc: usize,
        item: &T,
        size: &KdBox<C>,
        item_count: &mut i64,
        delete_flip: &mut bool,
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

            *delete_flip = !*delete_flip;
            let mut use_hi = *delete_flip;
            if node.sons[1].is_none() {
                use_hi = false;
            } else if node.sons[0].is_none() {
                use_hi = true;
            }

            if use_hi {
                let (q_item, q_size) = Self::find_extreme(node.sons[1].as_ref().unwrap(), next_disc(disc), disc, true, &mut 0i32);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size;

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[1] = Self::hard_delete_recursive(node.sons[1].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count, delete_flip);
            } else {
                let (q_item, q_size) = Self::find_extreme(node.sons[0].as_ref().unwrap(), next_disc(disc), disc, false, &mut 0i32);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size;

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[0] = Self::hard_delete_recursive(node.sons[0].take(), next_disc(disc), &q_item_clone, &q_size_clone, item_count, delete_flip);
            }
            return Some(node);
        }

        let val = size[disc] - node.size[disc];
        let next = next_disc(disc);

        let child_idx = if val == C::zero() {
            // Same tie ambiguity as `find_recursive` (see there for why) -- ask it
            // which side actually holds the item instead of guessing from this
            // node's other axes.
            if Self::contains_recursive(node.sons[0].as_deref(), next, item, size) { 0 } else { 1 }
        } else if val > C::zero() { 1 } else { 0 };

        node.sons[child_idx] = Self::hard_delete_recursive(node.sons[child_idx].take(), next, item, size, item_count, delete_flip);
        Some(node)
    }

    /// Physically deletes an item, mirroring `hard_delete` but reporting the number of
    /// candidate nodes examined (`tries`) and the number of cascade levels performed
    /// (`dels`) while restructuring the tree, matching the 3D crate's `really_delete`.
    pub fn really_delete(&mut self, item: &T, size: &KdBox<C>) -> (Status, i32, i32) {
        if Self::find_recursive(self.root.as_mut(), 0, item, size).is_none() {
            return (Status::NotFound, 0, 0);
        }

        let mut tries = 0i32;
        let mut dels = 0i32;
        self.root = Self::really_delete_recursive(self.root.take(), 0, item, size, &mut tries, &mut dels, &mut self.delete_flip);
        self.item_count -= 1;
        (Status::Ok, tries, dels)
    }

    fn really_delete_recursive(
        node_opt: Option<Box<Node<T, C>>>,
        disc: usize,
        item: &T,
        size: &KdBox<C>,
        tries: &mut i32,
        dels: &mut i32,
        delete_flip: &mut bool,
    ) -> Option<Box<Node<T, C>>> {
        let mut node = node_opt?;

        let is_match = match node.item {
            Some(ref node_item) => node_item == item,
            None => false,
        };

        if is_match {
            if node.sons[0].is_none() && node.sons[1].is_none() {
                return None;
            }

            *delete_flip = !*delete_flip;
            let mut use_hi = *delete_flip;
            if node.sons[1].is_none() {
                use_hi = false;
            } else if node.sons[0].is_none() {
                use_hi = true;
            }

            if use_hi {
                let (q_item, q_size) = Self::find_extreme(node.sons[1].as_ref().unwrap(), next_disc(disc), disc, true, tries);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size;

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[1] = Self::really_delete_recursive(node.sons[1].take(), next_disc(disc), &q_item_clone, &q_size_clone, tries, dels, delete_flip);
            } else {
                let (q_item, q_size) = Self::find_extreme(node.sons[0].as_ref().unwrap(), next_disc(disc), disc, false, tries);
                let q_item_clone = q_item.clone();
                let q_size_clone = q_size;

                node.item = Some(q_item_clone.clone());
                node.size = q_size_clone;
                node.sons[0] = Self::really_delete_recursive(node.sons[0].take(), next_disc(disc), &q_item_clone, &q_size_clone, tries, dels, delete_flip);
            }
            *dels += 1;
            return Some(node);
        }

        let val = size[disc] - node.size[disc];
        let next = next_disc(disc);

        let child_idx = if val == C::zero() {
            // Same tie ambiguity as `find_recursive` (see there for why) -- ask it
            // which side actually holds the item instead of guessing from this
            // node's other axes.
            if Self::contains_recursive(node.sons[0].as_deref(), next, item, size) { 0 } else { 1 }
        } else if val > C::zero() { 1 } else { 0 };

        node.sons[child_idx] = Self::really_delete_recursive(node.sons[child_idx].take(), next, item, size, tries, dels, delete_flip);
        Some(node)
    }

    fn find_extreme<'a>(
        node: &'a Node<T, C>,
        node_disc: usize,
        target_disc: usize,
        find_min: bool,
        tries: &mut i32,
    ) -> (&'a T, KdBox<C>) {
        *tries += 1;

        let mut best_item = node.item.as_ref().unwrap();
        let mut best_size = node.size;

        // Branch-and-bound pruning: once `node_disc` cycles back to the axis we are
        // optimizing (`target_disc`), the multidimensional-BST invariant tells us one
        // whole side can never improve on what this node already contributes, so it can
        // be skipped outright:
        //   - lo-son values are all strictly less than this node's own value at that
        //     axis, so they can never beat a find_max search;
        //   - hi-son values are all >= this node's own value at that axis, so they can
        //     never beat a find_min search.
        // This must be an *unconditional* skip rather than a running-best value
        // comparison: the unexplored side is only bounded on one end by this node's own
        // value, never on the other, so a value-based prune can (and did, in an earlier,
        // more literal port of the 3D crate's `find_min_max_node` logic) wrongly discard
        // a subtree that still contains the true extreme. See the 3D crate's
        // `find_min_max_node` for the identical fix.
        let skip_lo = node_disc == target_disc && !find_min;
        let skip_hi = node_disc == target_disc && find_min;

        if !skip_lo {
            if let Some(ref lo) = node.sons[0] {
                let (l_item, l_size) = Self::find_extreme(lo, next_disc(node_disc), target_disc, find_min, tries);
                let better = if find_min {
                    l_size[target_disc] < best_size[target_disc]
                } else {
                    l_size[target_disc] > best_size[target_disc]
                };
                if better {
                    best_size = l_size;
                    best_item = l_item;
                }
            }
        }

        if !skip_hi {
            if let Some(ref hi) = node.sons[1] {
                let (h_item, h_size) = Self::find_extreme(hi, next_disc(node_disc), target_disc, find_min, tries);
                let better = if find_min {
                    h_size[target_disc] < best_size[target_disc]
                } else {
                    h_size[target_disc] > best_size[target_disc]
                };
                if better {
                    best_size = h_size;
                    best_item = h_item;
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

    /// Prints balance diagnostics for the tree, matching the 3D crate's `badness`.
    pub fn badness(&self) {
        let mut factor3 = 0;
        let mut max_levels = 0;

        fn traverse<T, C: Coord>(node: &Option<Box<Node<T, C>>>, level: i32, factor3: &mut i32, max_levels: &mut i32) {
            if let Some(n) = node {
                let has_lo = n.sons[0].is_some();
                let has_hi = n.sons[1].is_some();
                if (has_lo || has_hi) && !(has_lo && has_hi) {
                    *factor3 += 1;
                }
                if level > *max_levels {
                    *max_levels = level;
                }
                traverse(&n.sons[0], level + 1, factor3, max_levels);
                traverse(&n.sons[1], level + 1, factor3, max_levels);
            }
        }

        traverse(&self.root, 1, &mut factor3, &mut max_levels);

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
    node.lo_min_bound = node.lo_min_bound.cmin(size[vert]);
    node.hi_max_bound = node.hi_max_bound.cmax(size[vert + 2]);
    if (disc & 0x2) != 0 {
        node.other_bound = node.other_bound.cmin(size[vert]);
    } else {
        node.other_bound = node.other_bound.cmax(size[vert + 2]);
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

/// A stack frame used by `kd_neighbor`. Unlike `Save` (used by `Generator`), each frame
/// also carries the tightened search-box bounds (`bp`/`bn`) accumulated on the path from
/// the root, matching the 3D crate's `Save<C>` frame (which stores the same bounds, just
/// keyed by arena index rather than a direct node reference).
struct NeighborSave<'a, T, C> {
    node: &'a Node<T, C>,
    disc: usize,
    state: State,
    bn: KdBox<C>,
    bp: KdBox<C>,
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

    /// Finds the `m` nearest neighbors to the point `(x, y)`.
    pub fn nearest(&self, x: C, y: C, m: usize) -> Vec<Priority<T>> {
        if self.root.is_none() || m == 0 {
            return Vec::new();
        }

        let mut list = vec![Priority { dist: f64::MAX, item: None }; m];
        let xq = [x, y, x, y];
        let bp = [C::max_value(); 4];
        let bn = [C::min_value(); 4];

        self.kd_neighbor(self.root.as_ref().unwrap(), &xq, m, &mut list, bp, bn);

        for p in &mut list {
            if p.dist != f64::MAX {
                p.dist = p.dist.sqrt();
            }
        }
        list
    }

    fn kd_neighbor(&self, root: &Node<T, C>, xq: &KdBox<C>, m: usize, list: &mut [Priority<T>], bp: KdBox<C>, bn: KdBox<C>) {
        let mut stack = Vec::new();
        stack.push(NeighborSave { node: root, disc: 0, state: State::ThisOne, bn, bp });

        while let Some(top) = stack.last_mut() {
            let node = top.node;
            let d = top.disc;
            let p = node.size[d];
            let hort = d & 0x01;
            let vert = (d & 0x02) != 0;

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
                        if let Some(ref child) = node.sons[0] {
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
                                let child_ref = &**child;
                                stack.push(NeighborSave { node: child_ref, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    } else {
                        if let Some(ref child) = node.sons[1] {
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
                                let child_ref = &**child;
                                stack.push(NeighborSave { node: child_ref, disc: next_disc(d), state: State::ThisOne, bn, bp });
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
                        if let Some(ref child) = node.sons[1] {
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
                                let child_ref = &**child;
                                stack.push(NeighborSave { node: child_ref, disc: next_disc(d), state: State::ThisOne, bn, bp });
                                let last = stack.len() - 2;
                                stack[last].bn[hort] = old_bn;
                                stack[last].bp[hort] = old_bp;
                                continue;
                            }
                            top.bn[hort] = old_bn;
                            top.bp[hort] = old_bp;
                        }
                    } else {
                        if let Some(ref child) = node.sons[0] {
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
                                let child_ref = &**child;
                                stack.push(NeighborSave { node: child_ref, disc: next_disc(d), state: State::ThisOne, bn, bp });
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
        for i in 0..2 {
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
}

impl<T: PartialEq + Clone, C: Coord> Tree<T, C> {
    pub fn serialize<F>(&self, filename: &str, mut item_to_id: F) -> std::io::Result<()>
    where
        F: FnMut(&T) -> u64,
    {
        use std::fs::File;
        use std::io::{BufWriter, Write};

        let active_count = self.item_count - self.dead_count;
        if active_count <= 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "Empty tree",
            ));
        }

        let mut vec: Vec<MmapNode<C>> = Vec::with_capacity(active_count as usize);

        fn serialize_node_recursive<T, C: Coord, F>(
            node_opt: &Option<Box<Node<T, C>>>,
            vec: &mut Vec<MmapNode<C>>,
            item_to_id: &mut F,
        ) -> i64
        where
            F: FnMut(&T) -> u64,
        {
            if let Some(ref node) = node_opt {
                if let Some(ref item) = node.item {
                    let my_idx = vec.len() as i64;
                    
                    // Push a placeholder node
                    vec.push(MmapNode {
                        source_id: 0,
                        size: node.size,
                        lo_min_bound: node.lo_min_bound,
                        hi_max_bound: node.hi_max_bound,
                        other_bound: node.other_bound,
                        left_child: -1,
                        right_child: -1,
                    });

                    // Recurse left and right
                    let left = serialize_node_recursive(&node.sons[0], vec, item_to_id);
                    let right = serialize_node_recursive(&node.sons[1], vec, item_to_id);

                    // Update values with actual values
                    vec[my_idx as usize].source_id = item_to_id(item);
                    vec[my_idx as usize].left_child = left;
                    vec[my_idx as usize].right_child = right;

                    return my_idx;
                }
            }
            -1
        }

        serialize_node_recursive(&self.root, &mut vec, &mut item_to_id);

        let file = File::create(filename)?;
        let mut writer = BufWriter::new(file);

        for node in vec {
            writer.write_all(&node.source_id.to_le_bytes())?;
            for val in node.size {
                val.write_to(&mut writer)?;
            }
            node.lo_min_bound.write_to(&mut writer)?;
            node.hi_max_bound.write_to(&mut writer)?;
            node.other_bound.write_to(&mut writer)?;
            writer.write_all(&node.left_child.to_le_bytes())?;
            writer.write_all(&node.right_child.to_le_bytes())?;
        }

        Ok(())
    }

    pub fn get_bounds(&self) -> Option<KdBox<C>> {
        let mut bounds: Option<KdBox<C>> = None;
        fn traverse<T, C: Coord>(node: &Option<Box<Node<T, C>>>, bounds: &mut Option<KdBox<C>>) {
            if let Some(n) = node {
                if n.item.is_some() {
                    if let Some(ref mut b) = bounds {
                        let dim = b.len() / 2;
                        for d in 0..dim {
                            if n.size[d] < b[d] {
                                b[d] = n.size[d];
                            }
                            if n.size[d + dim] > b[d + dim] {
                                b[d + dim] = n.size[d + dim];
                            }
                        }
                    } else {
                        *bounds = Some(n.size.clone());
                    }
                }
                traverse(&n.sons[0], bounds);
                traverse(&n.sons[1], bounds);
            }
        }
        traverse(&self.root, &mut bounds);
        bounds
    }
}

#[derive(Debug, Clone)]
pub struct MmapNode<C> {
    pub source_id: u64,
    pub size: KdBox<C>,
    pub lo_min_bound: C,
    pub hi_max_bound: C,
    pub other_bound: C,
    pub left_child: i64,
    pub right_child: i64,
}

pub fn get_mmap_bounds<C: Coord>(nodes: &[MmapNode<C>]) -> Option<KdBox<C>> {
    if nodes.is_empty() {
        return None;
    }

    let mut bounds: Option<KdBox<C>> = None;
    const DIM: usize = 2; // For 2D

    for node in nodes {
        if node.source_id != 0 {
            if let Some(ref mut b) = bounds {
                for d in 0..DIM {
                    if node.size[d] < b[d] {
                        b[d] = node.size[d];
                    }
                    if node.size[d + DIM] > b[d + DIM] {
                        b[d + DIM] = node.size[d + DIM];
                    }
                }
            } else {
                bounds = Some(node.size.clone());
            }
        }
    }

    bounds
}

pub fn get_serialized_bounds<C: Coord>(filename: &str) -> std::io::Result<Option<KdBox<C>>> {
    use std::fs::File;
    use std::io::{BufReader, Read, Seek, SeekFrom};

    let mut file = File::open(filename)?;
    let metadata = file.metadata()?;
    const DIM: usize = 2; // For 2D

    let t_size = std::mem::size_of::<C>();
    let record_size = (8 + (2 * DIM + 3) * t_size + 16) as i64;

    if metadata.len() >= record_size as u64 && metadata.len() % record_size as u64 == 0 {
        // Try O(1) fast sentinel check at the end of the file
        file.seek(SeekFrom::End(-record_size))?;
        let mut reader = BufReader::new(&mut file);

        let mut id_buf = [0u8; 8];
        reader.read_exact(&mut id_buf)?;
        let source_id = u64::from_le_bytes(id_buf);

        if source_id == u64::MAX {
            let mut size = [C::zero(); 2 * DIM];
            for val in size.iter_mut() {
                *val = C::read_from(&mut reader)?;
            }
            return Ok(Some(size));
        }
    }

    // Fallback to O(N) scan
    file.seek(SeekFrom::Start(0))?;
    let mut reader = BufReader::new(file);
    let mut nodes = Vec::new();

    loop {
        let mut id_buf = [0u8; 8];
        match reader.read_exact(&mut id_buf) {
            Ok(_) => {}
            Err(ref e) if e.kind() == std::io::ErrorKind::UnexpectedEof => break,
            Err(e) => return Err(e),
        }
        let source_id = u64::from_le_bytes(id_buf);

        let mut size = [C::zero(); 2 * DIM];
        for val in size.iter_mut() {
            *val = C::read_from(&mut reader)?;
        }

        let lo_min_bound = C::read_from(&mut reader)?;
        let hi_max_bound = C::read_from(&mut reader)?;
        let other_bound = C::read_from(&mut reader)?;

        let mut left_buf = [0u8; 8];
        reader.read_exact(&mut left_buf)?;
        let left_child = i64::from_le_bytes(left_buf);

        let mut right_buf = [0u8; 8];
        reader.read_exact(&mut right_buf)?;
        let right_child = i64::from_le_bytes(right_buf);

        nodes.push(MmapNode {
            source_id,
            size,
            lo_min_bound,
            hi_max_bound,
            other_bound,
            left_child,
            right_child,
        });
    }

    Ok(get_mmap_bounds(&nodes))
}

pub fn intersect<C: Coord>(b1: &KdBox<C>, b2: &KdBox<C>) -> bool {
    b1[RIGHT] >= b2[LEFT] &&
    b2[RIGHT] >= b1[LEFT] &&
    b1[TOP] >= b2[BOTTOM] &&
    b2[TOP] >= b1[BOTTOM]
}

pub fn kd_dist_sq<C: Coord>(xq: &KdBox<C>, box_val: &KdBox<C>) -> f64 {
    let mut dx = 0.0;
    let mut dy = 0.0;

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

    dx * dx + dy * dy
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

                const KD_BOXES: usize = 5000;
                const MIN_RANGE: i32 = -20000;
                const MAX_RANGE: i32 = 20000;
                const RANGE_SPAN: i32 = MAX_RANGE - MIN_RANGE + 1;
                const BOX_RANGE: i32 = 1000;

                fn rand_box(rng: &mut Lcg) -> KdBox<$t> {
                    let left = rng.next_range(RANGE_SPAN) + MIN_RANGE;
                    let bottom = rng.next_range(RANGE_SPAN) + MIN_RANGE;
                    let right = left + rng.next_range(BOX_RANGE);
                    let top = bottom + rng.next_range(BOX_RANGE);
                    [
                        <$t>::from_i32(left),
                        <$t>::from_i32(bottom),
                        <$t>::from_i32(right),
                        <$t>::from_i32(top),
                    ]
                }

                #[test]
                fn test_nearest() {
                    let mut rng = Lcg { state: 42 };
                    let mut boxes = Vec::new();
                    let mut tree = Tree::<usize, $t>::new();

                    for i in 0..KD_BOXES {
                        let b = rand_box(&mut rng);
                        boxes.push(b);
                        tree.insert(i, b);
                    }

                    for m in [1, 2, 4, 8, 16] {
                        for _ in 0..50 {
                            let qx = <$t>::from_i32(rng.next_range(RANGE_SPAN) + MIN_RANGE);
                            let qy = <$t>::from_i32(rng.next_range(RANGE_SPAN) + MIN_RANGE);

                            let list = tree.nearest(qx, qy, m);
                            assert_eq!(list.len(), m);

                            for i in 1..m {
                                assert!(list[i].dist >= list[i - 1].dist - 1e-9);
                            }

                            let mut brute: Vec<f64> = boxes.iter()
                                .map(|b| kd_dist_sq(&[qx, qy, qx, qy], b).sqrt())
                                .collect();
                            brute.sort_by(|a, b| a.partial_cmp(b).unwrap());

                            assert!(list[m - 1].dist <= brute[m - 1] + 1e-6);
                        }
                    }
                }

                #[test]
                fn test_really_delete() {
                    let mut rng = Lcg { state: 7 };
                    let mut tree = Tree::<usize, $t>::new();
                    let mut boxes = Vec::new();

                    // Regression coverage for a tie-break bug in the promote-and-cascade
                    // delete shared by `hard_delete`/`really_delete`: on an exact
                    // coordinate tie, `find_recursive` used to fall back to comparing
                    // *this node's* other axes to pick a side -- but delete's promotion
                    // step can swap a different item into a node's position later,
                    // changing those other-axis values without changing which subtree
                    // the original item was placed in, silently misrouting later
                    // searches and making an unrelated, never-deleted item unfindable.
                    // Fixed by having ties check (read-only) which side actually holds
                    // the item instead of guessing. Kept at 3000 items / 750 interleaved
                    // deletions -- the scale that reliably reproduced the bug before the
                    // fix -- specifically to catch a regression.
                    for i in 0..3000 {
                        let b = rand_box(&mut rng);
                        boxes.push(b);
                        tree.insert(i, b);
                    }

                    assert_eq!(tree.count(), 3000);

                    // Deleting an item that was never inserted must report NotFound and
                    // must not touch the tries/dels counters or the item count.
                    let missing_box: KdBox<$t> = [
                        <$t>::from_i32(1_000_000),
                        <$t>::from_i32(1_000_000),
                        <$t>::from_i32(1_000_001),
                        <$t>::from_i32(1_000_001),
                    ];
                    let (status, tries, dels) = tree.really_delete(&999_999usize, &missing_box);
                    assert_eq!(status, Status::NotFound);
                    assert_eq!(tries, 0);
                    assert_eq!(dels, 0);
                    assert_eq!(tree.count(), 3000);

                    for i in 0..750 {
                        let (status, tries, dels) = tree.really_delete(&i, &boxes[i]);
                        assert_eq!(status, Status::Ok);
                        assert!(tries >= 0);
                        assert!(dels >= 0);
                        assert!(!tree.is_member(&i, &boxes[i]));
                    }

                    assert_eq!(tree.count(), 2250);

                    for i in 750..3000 {
                        assert!(tree.is_member(&i, &boxes[i]), "item {} should still be present", i);
                    }
                }

                #[test]
                fn test_badness() {
                    let mut tree = Tree::<usize, $t>::new();
                    // Must not panic on an empty tree.
                    tree.badness();

                    let mut rng = Lcg { state: 99 };
                    for i in 0..2000 {
                        let b = rand_box(&mut rng);
                        tree.insert(i, b);
                    }
                    // Must not panic on a populated tree either.
                    tree.badness();
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

                #[test]
                fn test_serialize() {
                    let mut tree = Tree::<String, $t>::new();
                    let b1 = [<$t>::from_i32(0), <$t>::from_i32(0), <$t>::from_i32(10), <$t>::from_i32(10)];
                    let b2 = [<$t>::from_i32(20), <$t>::from_i32(20), <$t>::from_i32(30), <$t>::from_i32(30)];
                    let b3 = [<$t>::from_i32(5), <$t>::from_i32(5), <$t>::from_i32(15), <$t>::from_i32(15)];

                    tree.insert("item1".to_string(), b1);
                    tree.insert("item2".to_string(), b2);
                    tree.insert("item3".to_string(), b3);

                    // Test get_bounds
                    let bounds = tree.get_bounds().unwrap();
                    let expected_bounds: KdBox<$t> = [<$t>::from_i32(0), <$t>::from_i32(0), <$t>::from_i32(30), <$t>::from_i32(30)];
                    assert_eq!(bounds, expected_bounds);

                    let filename = format!("test_serialize_{}.kdtree", stringify!($mod_name));
                    let err = tree.serialize(&filename, |item| {
                        item.replace("item", "").parse::<u64>().unwrap()
                    });
                    assert!(err.is_ok());

                    let metadata = std::fs::metadata(&filename).unwrap();
                    let expected_size = 3 * (8 + 7 * std::mem::size_of::<$t>() + 16);
                    assert_eq!(metadata.len(), expected_size as u64);

                    // Test get_serialized_bounds
                    let ser_bounds = get_serialized_bounds::<$t>(&filename).unwrap().unwrap();
                    assert_eq!(ser_bounds, expected_bounds);

                    let _ = std::fs::remove_file(&filename);
                }
            }
        };
    }

    generate_tests!(i32, tests_i32);
    generate_tests!(i64, tests_i64);
    generate_tests!(i128, tests_i128);
    generate_tests!(f64, tests_f64);
}

#[cfg(test)]
mod geo_math_tests {
    use super::*;

    fn assert_close(a: f64, b: f64, tol: f64) {
        assert!((a - b).abs() < tol, "expected {} to be within {} of {}", a, tol, b);
    }

    macro_rules! generate_dms_tests {
        ($t:ty, $mod_name:ident) => {
            mod $mod_name {
                use super::*;

                #[test]
                fn test_dms_round_trip() {
                    let values: [f64; 6] = [0.0, 45.5, -45.5, 90.0, -90.0, 179.999999];
                    for &x in values.iter() {
                        let (sign, deg, min, sec) = degrees_to_dms::<$t>(x);
                        let round_tripped = dms_to_degrees::<$t>(sign, deg, min, sec);
                        assert!(
                            (round_tripped - x).abs() < 1e-9,
                            "round trip failed for {}: got {}",
                            x,
                            round_tripped
                        );
                    }
                }

                #[test]
                fn test_dms_negative_near_zero() {
                    // sign=-1, deg=0, min=15, sec=0.0 => -0.25 degrees exactly.
                    let degrees = dms_to_degrees::<$t>(-1, <$t>::from_i32(0), <$t>::from_i32(15), 0.0);
                    assert_eq!(degrees, -0.25);
                }
            }
        };
    }

    generate_dms_tests!(i32, dms_i32);
    generate_dms_tests!(i64, dms_i64);
    generate_dms_tests!(i128, dms_i128);
    generate_dms_tests!(f64, dms_f64);

    #[test]
    fn test_haversine_quarter_great_circle() {
        let radius = 6371.0;
        let dist = haversine_distance(0.0, 0.0, 90.0, 0.0, radius);
        let expected = radius * std::f64::consts::PI / 2.0;
        assert_close(dist, expected, 1e-9);
    }

    #[test]
    fn test_vincenty_equator() {
        let delta: f64 = 10.0;
        let dist = vincenty_distance(0.0, 0.0, 0.0, delta, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING);
        let expected = EARTH_SEMI_MAJOR_AXIS_M * delta.to_radians();
        assert_close(dist, expected, 1e-6);
    }

    #[test]
    fn test_vincenty_near_antipodal_is_finite() {
        let dist = vincenty_distance(0.0, 0.0, 0.001, 179.999, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING);
        assert!(dist.is_finite());
    }

    // Expected values cross-checked against C/healpix_calc.c (via the shared geo_utils.c
    // implementation it now wraps), which this port mirrors exactly.
    #[test]
    fn test_healpix_nested_index_matches_c_reference() {
        assert_eq!(healpix_nested_index(217.4290, -62.6795, 12), 134053741); // polar cap
        assert_eq!(healpix_nested_index(-109.05653, 44.52634, 3), 330); // polar cap, negative lon
        assert_eq!(healpix_nested_index(45.0, 10.0, 3), 282); // equatorial belt
        assert_eq!(healpix_nested_index(0.0, 0.0, 3), 256); // equatorial belt, origin
        assert_eq!(healpix_nested_index(200.0, -20.0, 5), 4257); // equatorial belt, higher level
    }

    #[test]
    fn test_healpix_nested_index_longitude_normalization() {
        // -109.05653 and its +360 equivalent must land in the same cell.
        assert_eq!(
            healpix_nested_index(-109.05653, 44.52634, 3),
            healpix_nested_index(250.94347, 44.52634, 3)
        );
    }
}
