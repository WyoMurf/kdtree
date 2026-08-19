struct NameEntry
    name::String
    population::Int64
end

# Reads cities.names (tab-separated: geonameid, name, population) into a
# plain Dict keyed by geonameid.
#
# earth_viewer.c hand-rolls an open-addressing hash table over one
# contiguous array specifically to avoid ~170k individually malloc'd chain
# nodes scattered across the heap - profiling there showed that chain-of-
# pointers version was responsible for roughly a third of this viewer's
# per-frame CPU cost, almost all cache misses. Julia's built-in Dict
# doesn't have that failure mode (one contiguous table, no per-entry heap
# node), so it's the direct equivalent of the *fix*, not a step back from
# it - no custom hash table needed here.
function load_names(path::AbstractString)
    names = Dict{UInt64,NameEntry}()

    if !isfile(path)
        println("Warning: could not open $path, city names will be unavailable.")
        return names
    end

    for line in eachline(path)
        parts = split(line, '\t'; limit=3)
        length(parts) != 3 && continue
        id = tryparse(UInt64, parts[1])
        id === nothing && continue
        population = something(tryparse(Int64, parts[3]), Int64(0))
        names[id] = NameEntry(parts[2], population)
    end

    println("Loaded $(length(names)) city names.")
    return names
end
