use std::collections::HashMap;
use std::fs;

/// One row of cities.names: geonameid, display name, population.
pub struct NameEntry {
    pub name: String,
    pub population: i64,
}

/// Reads cities.names (tab-separated: geonameid, name, population) into a
/// plain map keyed by geonameid.
///
/// earth_viewer.c hand-rolls an open-addressing hash table over one
/// contiguous array specifically to avoid ~170k individually malloc'd
/// chain nodes scattered across the heap - profiling there showed that
/// chain-of-pointers version was responsible for roughly a third of this
/// viewer's per-frame CPU cost, almost all cache misses. A plain
/// `HashMap` doesn't have that failure mode (one contiguous table, no
/// per-entry allocation), so it's the direct equivalent of the *fix*, not
/// a step back from it - no custom hash table needed here.
pub fn load_names(path: &str) -> HashMap<u64, NameEntry> {
    let mut names = HashMap::new();

    let data = match fs::read_to_string(path) {
        Ok(d) => d,
        Err(_) => {
            eprintln!("Warning: could not open {path}, city names will be unavailable.");
            return names;
        }
    };

    for line in data.lines() {
        let mut parts = line.splitn(3, '\t');
        let (Some(id_str), Some(name), Some(pop_str)) = (parts.next(), parts.next(), parts.next())
        else {
            continue;
        };
        let Ok(id) = id_str.parse::<u64>() else {
            continue;
        };
        let population = pop_str.parse::<i64>().unwrap_or(0);
        names.insert(
            id,
            NameEntry {
                name: name.to_string(),
                population,
            },
        );
    }

    println!("Loaded {} city names.", names.len());
    names
}
