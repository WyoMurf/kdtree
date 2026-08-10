module KDTree

export serialize, get_bounds, get_serialized_bounds, get_mmap_bounds, MmapNode, Tree, Box, insert!, count_items, is_member, hard_delete!, really_delete!, badness, nearest, Priority, search, LEFT, BOTTOM, RIGHT, TOP,
       haversine_distance, vincenty_distance, dms_to_degrees, degrees_to_dms, EARTH_RADIUS_KM, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING,
       healpix_nested_index

const Box{C<:Real} = NTuple{4, C}
const LEFT = 1
const BOTTOM = 2
const RIGHT = 3
const TOP = 4

struct MmapNode{N, C<:Real}
    source_id::UInt64
    size::NTuple{N, C}
    lo_min_bound::C
    hi_max_bound::C
    other_bound::C
    left_child::Int64
    right_child::Int64
end

mutable struct Node{T, C<:Real}
    item::Union{T, Nothing}
    size::Box{C}
    lo_min_bound::C
    hi_max_bound::C
    other_bound::C
    sons::Vector{Union{Node{T, C}, Nothing}}
    
    function Node{T, C}(item, size::Box{C}, lo::C, hi::C, ob::C) where {T, C<:Real}
        new{T, C}(item, size, lo, hi, ob, Union{Node{T, C}, Nothing}[nothing, nothing])
    end
end

mutable struct Tree{T, C<:Real}
    root::Union{Node{T, C}, Nothing}
    item_count::Int
    dead_count::Int
    extent::Vector{C}
    
    function Tree{T, C}() where {T, C<:Real}
        new{T, C}(nothing, 0, 0, zeros(C, 4))
    end
end

# -----------------------------------------------------------------------------
# General-purpose geo / angle utilities
#
# These have nothing to do with the kd-tree data structure itself; they are
# exported alongside it purely as convenient general-purpose geo/angle math.
# All lat/lon angle inputs and outputs are in degrees.
# -----------------------------------------------------------------------------

"""
    haversine_distance(lat1, lon1, lat2, lon2, radius)

Great-circle distance between two (lat, lon) points (given in degrees) on a
perfect sphere of the given `radius`, using the Haversine formula. Fast and
approximate: it models the body as a perfect sphere rather than an oblate
spheroid. For higher accuracy on an ellipsoidal model (e.g. WGS-84 Earth),
use [`vincenty_distance`](@ref) instead.
"""
function haversine_distance(lat1::Float64, lon1::Float64, lat2::Float64, lon2::Float64, radius::Float64)
    to_rad = pi / 180.0
    phi1 = lat1 * to_rad
    phi2 = lat2 * to_rad
    dphi = (lat2 - lat1) * to_rad
    dlambda = (lon2 - lon1) * to_rad
    a = sin(dphi / 2.0)^2 + cos(phi1) * cos(phi2) * sin(dlambda / 2.0)^2
    c = 2.0 * atan(sqrt(a), sqrt(1.0 - a))
    return radius * c
end

# WGS-84 values -- pass these to vincenty_distance when modeling Earth specifically.
const EARTH_RADIUS_KM = 6371.0
const EARTH_SEMI_MAJOR_AXIS_M = 6378137.0
const EARTH_FLATTENING = 1.0 / 298.257223563

"""
    vincenty_distance(lat1, lon1, lat2, lon2, semi_major_axis, flattening)

Distance between two (lat, lon) points (given in degrees) on an oblate
spheroid defined by `semi_major_axis` and `flattening`, using Vincenty's
iterative formula. Slower than [`haversine_distance`](@ref), but exact on the
modeled spheroid rather than approximating a perfect sphere. For Earth
specifically, pass the WGS-84 constants `EARTH_SEMI_MAJOR_AXIS_M` and
`EARTH_FLATTENING`.

Note: Vincenty's iteration is known to fail to fully converge for nearly
antipodal points (a known limitation of the algorithm). The 200-iteration cap
below exists so this still returns a best-effort finite value in that case,
rather than looping forever.
"""
function vincenty_distance(lat1::Float64, lon1::Float64, lat2::Float64, lon2::Float64, semi_major_axis::Float64, flattening::Float64)
    to_rad = pi / 180.0
    a = semi_major_axis
    f = flattening
    b = a * (1.0 - f)
    lat1_r = lat1 * to_rad
    lat2_r = lat2 * to_rad
    l = (lon2 - lon1) * to_rad
    u1 = atan((1.0 - f) * tan(lat1_r))
    u2 = atan((1.0 - f) * tan(lat2_r))
    sin_u1, cos_u1 = sin(u1), cos(u1)
    sin_u2, cos_u2 = sin(u2), cos(u2)

    lambda = l
    sin_sigma = 0.0
    cos_sigma = 0.0
    sigma = 0.0
    cos_sq_alpha = 0.0
    cos2_sigma_m = 0.0

    for _ in 1:200
        sin_lambda, cos_lambda = sin(lambda), cos(lambda)
        sin_sigma = sqrt((cos_u2 * sin_lambda)^2 + (cos_u1 * sin_u2 - sin_u1 * cos_u2 * cos_lambda)^2)
        if sin_sigma == 0.0
            return 0.0  # coincident points
        end
        cos_sigma = sin_u1 * sin_u2 + cos_u1 * cos_u2 * cos_lambda
        sigma = atan(sin_sigma, cos_sigma)
        sin_alpha = cos_u1 * cos_u2 * sin_lambda / sin_sigma
        cos_sq_alpha = 1.0 - sin_alpha^2
        cos2_sigma_m = cos_sq_alpha != 0.0 ? cos_sigma - 2.0 * sin_u1 * sin_u2 / cos_sq_alpha : 0.0
        c = f / 16.0 * cos_sq_alpha * (4.0 + f * (4.0 - 3.0 * cos_sq_alpha))
        lambda_prev = lambda
        lambda = l + (1.0 - c) * f * sin_alpha * (sigma + c * sin_sigma * (cos2_sigma_m + c * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m^2)))
        if abs(lambda - lambda_prev) < 1e-12
            break
        end
    end
    # after loop (converged or hit the 200-iteration cap -- either way, compute and return the best estimate, do not error)
    u_sq = cos_sq_alpha * (a^2 - b^2) / b^2
    big_a = 1.0 + u_sq / 16384.0 * (4096.0 + u_sq * (-768.0 + u_sq * (320.0 - 175.0 * u_sq)))
    big_b = u_sq / 1024.0 * (256.0 + u_sq * (-128.0 + u_sq * (74.0 - 47.0 * u_sq)))
    delta_sigma = big_b * sin_sigma * (cos2_sigma_m + big_b / 4.0 * (cos_sigma * (-1.0 + 2.0 * cos2_sigma_m^2) - big_b / 6.0 * cos2_sigma_m * (-3.0 + 4.0 * sin_sigma^2) * (-3.0 + 4.0 * cos2_sigma_m^2)))
    return b * big_a * (sigma - delta_sigma)
end

"""
    dms_to_degrees(sign, deg, min, sec) where {C<:Real}

Combine a sign (`+1` or `-1`), non-negative degree/minute magnitudes, and a
`Float64` seconds value into signed decimal degrees. Degrees and minutes are
always non-negative magnitudes; `sign` carries the sign separately so that
angles between -1 and 0 degrees (e.g. -0 deg 15 min) are representable, which
signed-degrees-only cannot do.
"""
function dms_to_degrees(sign::Int, deg::C, min::C, sec::Float64) where {C<:Real}
    return sign * (Float64(deg) + Float64(min) / 60.0 + sec / 3600.0)
end

"""
    degrees_to_dms(::Type{C}, degrees) where {C<:Real}

Split signed decimal `degrees` into `(sign, deg, min, sec)`, where `deg` and
`min` are non-negative magnitudes of type `C` and `sec` is `Float64`. `C`
must be given explicitly as the first argument since, unlike Rust or Go,
Julia cannot infer a return-type parameter from nothing -- e.g. callers write
`degrees_to_dms(Int32, 45.5)`.
"""
function degrees_to_dms(::Type{C}, degrees::Float64) where {C<:Real}
    sign = degrees < 0 ? -1 : 1
    a = abs(degrees)
    deg_f = floor(a)
    rem_min = (a - deg_f) * 60.0
    min_f = floor(rem_min)
    sec = (rem_min - min_f) * 60.0
    return sign, C(deg_f), C(min_f), sec
end

"""
    interleave_bits(x, y)

Interleaves the bits of two 32-bit integers into a 64-bit Morton (Z-order
curve) code -- the standard way to build a HEALPix NESTED pixel index from a
face's local (i, j) grid coordinates.
"""
function interleave_bits(x::UInt32, y::UInt32)::UInt64
    res = UInt64(0)
    for i in 0:31
        res |= (UInt64(x & (UInt32(1) << i)) << i) | (UInt64(y & (UInt32(1) << i)) << (i + 1))
    end
    return res
end

"""
    healpix_nested_index(ra_or_lon_deg, dec_or_lat_deg, level)

Converts an equatorial-style (ra, dec) or geographic (lon, lat) pair, in
degrees, into a HEALPix NESTED-scheme pixel index at the given resolution
`level` (nside = 2^level; 12*nside^2 cells total over the whole sphere --
level 3 is 768 cells, a common "roughly a thousand tiles" choice; valid
levels are 0..29). The two angle arguments are mathematically
interchangeable -- this is the same equatorial-coordinate projection either
way -- so pass right ascension/declination for astronomical data, or
longitude/latitude for terrestrial data. `ra_or_lon_deg` is normalized
internally, so it may be given in either the conventional [0, 360)
astronomical range or the conventional [-180, 180) geographic range;
callers don't need to pre-normalize longitude before calling.
"""
function healpix_nested_index(ra_or_lon_deg::Float64, dec_or_lat_deg::Float64, level::Int)::UInt64
    half_pi = pi / 2.0

    lon = mod(ra_or_lon_deg, 360.0)

    phi = lon * (pi / 180.0)
    z = sin(dec_or_lat_deg * (pi / 180.0))

    nside = UInt64(1) << level
    face_pixels = nside * nside

    local xc, yc
    if abs(z) <= 2.0 / 3.0
        xc = phi
        yc = 1.5 * z
    else
        # Polar caps.
        sgn = z >= 0.0 ? 1.0 : -1.0
        sigma = sqrt(3.0 * (1.0 - abs(z)))
        yc = sgn * (2.0 - sigma)

        # Find which of the 4 polar facets we are in.
        facet = floor(Int, phi / half_pi)
        facet = clamp(facet, 0, 3)
        phi_c = (facet + 0.5) * half_pi
        xc = phi_c + (phi - phi_c) * sigma
    end

    # Project to oblique grid coordinates (scaled by pi/2).
    pa = xc / half_pi
    pb = yc / half_pi

    u = pa + pb / 2.0
    v = pa - pb / 2.0

    ku = floor(u)
    kv = floor(v)
    u_frac = u - ku
    v_frac = v - kv

    # Translate (ku, kv) oblique grid coordinate to base face ID (0..11).
    ku_i = Int(ku)
    kv_i = Int(kv)

    face = UInt64(0)
    if ku_i >= 0 && kv_i >= 0
        if ku_i < 4 && kv_i < 4
            face = UInt64(mod(4 - kv_i + mod(ku_i, 4), 4) + 4) # Equatorial
        else
            face = UInt64(mod(ku_i, 4)) # North cap
        end
    elseif ku_i < 0 && kv_i < 0
        face = UInt64(8 + mod(ku_i, 4)) # South cap
    end

    # Grid coordinates inside the face.
    i = UInt32(floor(u_frac * nside))
    j = UInt32(floor(v_frac * nside))
    if i >= nside
        i = UInt32(nside - 1)
    end
    if j >= nside
        j = UInt32(nside - 1)
    end

    # Interleave bits for NESTED scheme.
    morton = interleave_bits(i, j)
    return face * face_pixels + morton
end

next_disc(disc::Int) = (disc % 4) + 1

function bounds_update!(node::Node{T, C}, disc::Int, size::Box{C}) where {T, C<:Real}
    vert = ((disc - 1) & 1) + 1
    node.lo_min_bound = min(node.lo_min_bound, size[vert])
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 2])
    if ((disc - 1) & 2) != 0
        node.other_bound = min(node.other_bound, size[vert])
    else
        node.other_bound = max(node.other_bound, size[vert + 2])
    end
end

function insert!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Real}
    if tree.root === nothing
        tree.root = Node{T, C}(item, size, size[LEFT], size[RIGHT], size[LEFT])
        tree.extent = [size[1], size[2], size[3], size[4]]
        tree.item_count = 1
        return
    end

    if insert_recursive!(tree.root, 1, item, size)
        tree.item_count += 1
        tree.extent[LEFT] = min(tree.extent[LEFT], size[LEFT])
        tree.extent[BOTTOM] = min(tree.extent[BOTTOM], size[BOTTOM])
        tree.extent[RIGHT] = max(tree.extent[RIGHT], size[RIGHT])
        tree.extent[TOP] = max(tree.extent[TOP], size[TOP])
    end
end

function insert_recursive!(elem::Node{T, C}, disc::Int, item::T, size::Box{C}) where {T, C<:Real}
    if elem.item !== nothing && elem.item == item
        return false
    end

    val = size[disc] - elem.size[disc]
    if val == 0
        ndisc = next_disc(disc)
        while ndisc != disc
            val = size[ndisc] - elem.size[ndisc]
            if val != 0
                break
            end
            ndisc = next_disc(ndisc)
        end
        if val == 0
            val = 1
        end
    end

    child_idx = val >= 0 ? 2 : 1

    if elem.sons[child_idx] !== nothing
        inserted = insert_recursive!(elem.sons[child_idx], next_disc(disc), item, size)
        if inserted
            bounds_update!(elem, disc, size)
        end
        return inserted
    end

    vert = ((next_disc(disc) - 1) & 1) + 1
    lo = size[vert]
    hi = size[vert + 2]
    ob = (((next_disc(disc) - 1) & 2) != 0) ? size[vert] : size[vert + 2]

    new_node = Node{T, C}(item, size, lo, hi, ob)
    elem.sons[child_idx] = new_node
    bounds_update!(elem, disc, size)
    return true
end

function intersect(b1::Box{C}, b2::Box{C}) where {C<:Real}
    return b1[RIGHT] >= b2[LEFT] &&
           b2[RIGHT] >= b1[LEFT] &&
           b1[TOP] >= b2[BOTTOM] &&
           b2[TOP] >= b1[BOTTOM]
end

function search(tree::Tree{T, C}, extent::Box{C}) where {T, C<:Real}
    results = Vector{Tuple{T, Box{C}}}()
    if tree.root === nothing
        return results
    end
    
    stack = Tuple{Node{T, C}, Int, Int}[]
    push!(stack, (tree.root, 1, 0))
    
    while !isempty(stack)
        node, disc, state = pop!(stack)
        hort = ((disc - 1) & 1) + 1
        
        if state == 0
            push!(stack, (node, disc, 1))
            if node.item !== nothing && intersect(extent, node.size)
                push!(results, (node.item, node.size))
            end
        elseif state == 1
            push!(stack, (node, disc, 2))
            if node.sons[1] !== nothing
                should_push = false
                if ((disc - 1) & 2) != 0
                    if extent[hort] <= node.size[disc] && extent[hort+2] >= node.lo_min_bound
                        should_push = true
                    end
                else
                    if extent[hort] <= node.other_bound && extent[hort+2] >= node.lo_min_bound
                        should_push = true
                    end
                end
                if should_push
                    push!(stack, (node.sons[1], next_disc(disc), 0))
                end
            end
        elseif state == 2
            if node.sons[2] !== nothing
                should_push = false
                if ((disc - 1) & 2) != 0
                    if extent[hort] <= node.hi_max_bound && extent[hort+2] >= node.other_bound
                        should_push = true
                    end
                else
                    if extent[hort] <= node.hi_max_bound && extent[hort+2] >= node.size[disc]
                        should_push = true
                    end
                end
                if should_push
                    push!(stack, (node.sons[2], next_disc(disc), 0))
                end
            end
        end
    end
    return results
end

function hard_delete!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Real}
    initial_count = tree.item_count
    tree.root = hard_delete_recursive!(tree.root, 1, item, size, tree)
    return tree.item_count < initial_count
end

function hard_delete_recursive!(node::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}, tree::Tree{T, C}) where {T, C<:Real}
    if node === nothing
        return nothing
    end

    if node.item !== nothing && node.item == item
        if node.sons[1] === nothing && node.sons[2] === nothing
            tree.item_count -= 1
            return nothing
        end

        if node.sons[2] !== nothing
            q_item, q_size = find_extreme(node.sons[2], next_disc(disc), disc, true)
            node.item = q_item
            node.size = q_size
            node.sons[2] = hard_delete_recursive!(node.sons[2], next_disc(disc), q_item, q_size, tree)
        else
            q_item, q_size = find_extreme(node.sons[1], next_disc(disc), disc, false)
            node.item = q_item
            node.size = q_size
            node.sons[1] = hard_delete_recursive!(node.sons[1], next_disc(disc), q_item, q_size, tree)
        end
        return node
    end

    val = size[disc] - node.size[disc]
    next = next_disc(disc)

    if val == 0
        # Same tie ambiguity as `find_recursive` (see there for why) -- ask it
        # which side actually holds the item instead of guessing from this
        # node's other axes.
        child_idx = find_recursive(node.sons[1], next, item, size) ? 1 : 2
    else
        child_idx = val >= 0 ? 2 : 1
    end

    node.sons[child_idx] = hard_delete_recursive!(node.sons[child_idx], next, item, size, tree)
    return node
end

function find_extreme(node::Node{T, C}, node_disc::Int, target_disc::Int, find_min::Bool) where {T, C<:Real}
    best_item = node.item
    best_size = node.size

    search_loson = node.sons[1] !== nothing
    search_hison = node.sons[2] !== nothing

    if node_disc == target_disc
        if find_min
            search_hison = false
        else
            search_loson = false
        end
    end

    if search_loson
        l_item, l_size = find_extreme(node.sons[1], next_disc(node_disc), target_disc, find_min)
        if find_min
            if l_size[target_disc] < best_size[target_disc]
                best_size = l_size
                best_item = l_item
            end
        else
            if l_size[target_disc] > best_size[target_disc]
                best_size = l_size
                best_item = l_item
            end
        end
    end

    if search_hison
        h_item, h_size = find_extreme(node.sons[2], next_disc(node_disc), target_disc, find_min)
        if find_min
            if h_size[target_disc] < best_size[target_disc]
                best_size = h_size
                best_item = h_item
            end
        else
            if h_size[target_disc] > best_size[target_disc]
                best_size = h_size
                best_item = h_item
            end
        end
    end

    return best_item, best_size
end

function find_recursive(elem::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}) where {T, C<:Real}
    if elem === nothing
        return false
    end
    if elem.item !== nothing && elem.item == item
        return true
    end

    val = size[disc] - elem.size[disc]

    if val == 0
        # Exact tie on this node's split axis: the item may legitimately live in
        # either subtree. We can't resolve this the way insert! does (comparing
        # this node's *other* axes), because kd_do_delete!'s promotion step can
        # later swap a different item into this exact tree position, changing
        # those other-axis values without changing which subtree the original
        # item was placed in -- that would silently misroute this search. Try
        # both sides instead of guessing.
        return find_recursive(elem.sons[1], next_disc(disc), item, size) ||
               find_recursive(elem.sons[2], next_disc(disc), item, size)
    end

    child_idx = val >= 0 ? 2 : 1
    return find_recursive(elem.sons[child_idx], next_disc(disc), item, size)
end

is_member(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Real} = find_recursive(tree.root, 1, item, size)
count_items(tree::Tree{T, C}) where {T, C<:Real} = tree.item_count - tree.dead_count

function node_cmp(a::Node{T, C}, b::Node{T, C}, disc::Int) where {T, C<:Real}
    val = a.size[disc] - b.size[disc]
    if val == 0
        new_disc = next_disc(disc)
        while new_disc != disc
            val = a.size[new_disc] - b.size[new_disc]
            if val != 0
                break
            end
            new_disc = next_disc(new_disc)
        end
        if val == 0
            val = 1
        end
    end
    return val >= 0
end

struct FindSave{T, C<:Real}
    node::Node{T, C}
    disc::Int
    state::Ref{Int}
end

function find_min_max_node!(t::Tree{T, C}, j::Int, kd_minval_node::Ref{Node{T, C}}, kd_minval_nodesdad::Ref{Node{T, C}}, dir::Ref{Int}, newj::Ref{Int}) where {T, C<:Real}
    kd_data_tries = 0
    stack = FindSave{T, C}[
        FindSave{T, C}(kd_minval_node[], next_disc(j), Ref(-1))
    ]

    if dir[] == 2 # HISON
        while !isempty(stack)
            top = stack[end]
            top_item = top.node
            m = top.disc

            if top.state[] == -1 # KD_THIS_ONE
                kd_data_tries += 1
                if top_item.item !== nothing && !node_cmp(top_item, kd_minval_node[], j) && top_item !== kd_minval_node[]
                    kd_minval_node[] = top_item
                    kd_minval_nodesdad[] = stack[end-1].node
                    if kd_minval_node[] === kd_minval_nodesdad[].sons[1]
                        dir[] = 1
                    else
                        dir[] = 2
                    end
                    newj[] = m
                end
                top.state[] += 1
            elseif top.state[] == 0 # LOSON
                if top_item.sons[1] !== nothing
                    top.state[] += 1
                    push!(stack, FindSave{T, C}(top_item.sons[1], next_disc(m), Ref(-1)))
                else
                    top.state[] += 1
                end
            elseif top.state[] == 1 # HISON
                if j == m && top_item.size[m] > kd_minval_node[].size[m]
                    top.state[] += 1
                else
                    if top_item.sons[2] !== nothing
                        top.state[] += 1
                        push!(stack, FindSave{T, C}(top_item.sons[2], next_disc(m), Ref(-1)))
                    else
                        top.state[] += 1
                    end
                end
            else
                pop!(stack)
            end
        end
        return kd_data_tries
    else # LOSON
        while !isempty(stack)
            top = stack[end]
            top_item = top.node
            m = top.disc

            if top.state[] == -1 # KD_THIS_ONE
                kd_data_tries += 1
                if top_item.item !== nothing && node_cmp(top_item, kd_minval_node[], j) && top_item !== kd_minval_node[]
                    kd_minval_node[] = top_item
                    kd_minval_nodesdad[] = stack[end-1].node
                    if kd_minval_node[] === kd_minval_nodesdad[].sons[1]
                        dir[] = 1
                    else
                        dir[] = 2
                    end
                    newj[] = m
                end
                top.state[] += 1
            elseif top.state[] == 0 # LOSON
                if j == m && top_item.size[m] < kd_minval_node[].size[m]
                    top.state[] += 1
                else
                    if top_item.sons[1] !== nothing
                        top.state[] += 1
                        push!(stack, FindSave{T, C}(top_item.sons[1], next_disc(m), Ref(-1)))
                    else
                        top.state[] += 1
                    end
                end
            elseif top.state[] == 1 # HISON
                if top_item.sons[2] !== nothing
                    top.state[] += 1
                    push!(stack, FindSave{T, C}(top_item.sons[2], next_disc(m), Ref(-1)))
                else
                    top.state[] += 1
                end
            else
                pop!(stack)
            end
        end
        return kd_data_tries
    end
end

mutable struct DeleteStats
    num_tries::Int
    num_del::Int
end

const delete_flip = Ref(false)

function find_item_with_path(node::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}, path::Vector{Node{T, C}}) where {T, C<:Real}
    if node === nothing
        return nothing, path
    end
    if node.item !== nothing && node.item == item
        return node, path
    end

    val = size[disc] - node.size[disc]
    next = next_disc(disc)

    if val == 0
        # Same tie ambiguity as `find_recursive` -- try both sides instead of
        # guessing via this node's other axes, since kd_do_delete!'s promotion
        # step can swap a different item into this position later. Each
        # attempt pushes onto its own fresh copy of the *original* path, so a
        # failed attempt's push never leaks into the path returned by
        # whichever side actually holds the item.
        lo_path = copy(path)
        push!(lo_path, node)
        found, new_path = find_item_with_path(node.sons[1], next, item, size, lo_path)
        if found !== nothing
            return found, new_path
        end
        hi_path = copy(path)
        push!(hi_path, node)
        return find_item_with_path(node.sons[2], next, item, size, hi_path)
    end

    child_idx = val >= 0 ? 2 : 1

    if node.sons[child_idx] !== nothing
        new_path = copy(path)
        push!(new_path, node)
        return find_item_with_path(node.sons[child_idx], next, item, size, new_path)
    end

    return nothing, path
end

function kd_do_delete!(t::Tree{T, C}, elem::Node{T, C}, j::Int, stats::DeleteStats) where {T, C<:Real}
    delete_flip[] = !delete_flip[]

    if elem.sons[1] === nothing && elem.sons[2] === nothing
        return nothing
    end

    Q_ref = Ref{Node{T, C}}()
    Qdad_ref = Ref{Node{T, C}}(elem)
    Qson_ref = Ref{Int}(0)
    newj_ref = Ref{Int}(0)

    if elem.sons[2] === nothing
        delete_flip[] = false
    elseif elem.sons[1] === nothing
        delete_flip[] = true
    end

    if !delete_flip[] # loson (sons[1])
        Q_ref[] = elem.sons[1]
        Qson_ref[] = 1
        newj_ref[] = next_disc(j)
        stats.num_tries += find_min_max_node!(t, j, Q_ref, Qdad_ref, Qson_ref, newj_ref)
    else # hison (sons[2])
        Q_ref[] = elem.sons[2]
        Qson_ref[] = 2
        newj_ref[] = next_disc(j)
        stats.num_tries += find_min_max_node!(t, j, Q_ref, Qdad_ref, Qson_ref, newj_ref)
    end

    Q = Q_ref[]
    Qdad = Qdad_ref[]
    Qson = Qson_ref[]
    newj = newj_ref[]

    Qdad.sons[Qson] = kd_do_delete!(t, Q, newj, stats)
    stats.num_del += 1
    Q.sons[1] = elem.sons[1]
    Q.sons[2] = elem.sons[2]
    Q.lo_min_bound = elem.lo_min_bound
    Q.other_bound = elem.other_bound
    Q.hi_max_bound = elem.hi_max_bound
    return Q
end

function really_delete!(tree::Tree{T, C}, item::T, old_size::Box{C}) where {T, C<:Real}
    elem, path = find_item_with_path(tree.root, 1, item, old_size, Node{T, C}[])
    if elem === nothing
        return (-4, 0, 0)
    end

    stats = DeleteStats(0, 1)

    if elem === tree.root
        tree.root = kd_do_delete!(tree, elem, 1, stats)
    else
        parent = path[end]
        j = (length(path) % 4) + 1
        new_elem = kd_do_delete!(tree, elem, j, stats)
        if parent.sons[2] === elem
            parent.sons[2] = new_elem
        else
            parent.sons[1] = new_elem
        end
    end

    tree.item_count -= 1
    return (1, stats.num_tries, stats.num_del)
end

function badness(tree::Tree{T, C}) where {T, C<:Real}
    factor3 = 0
    max_levels = 0

    function stats!(node, level)
        if node === nothing
            return
        end
        if (node.sons[1] !== nothing || node.sons[2] !== nothing) &&
           !(node.sons[1] !== nothing && node.sons[2] !== nothing)
            factor3 += 1
        end
        if level > max_levels
            max_levels = level
        end
        stats!(node.sons[1], level + 1)
        stats!(node.sons[2], level + 1)
    end

    stats!(tree.root, 1)

    targdepth = tree.item_count > 0 ? floor(log2(tree.item_count)) + 1 : 0
    ratio = targdepth > 0 ? max_levels / targdepth : 0.0

    dead_pct = tree.item_count > 0 ? (tree.dead_count / tree.item_count) * 100.0 : 0.0
    factor3_pct = tree.item_count > 0 ? (factor3 / tree.item_count) * 100.0 : 0.0

    println("balance ratio=$ratio (the closer to 1.0, the better), #of nodes with only one branch=$factor3 ($factor3_pct), max depth=$max_levels, dead=$(tree.dead_count) ($dead_pct)")
end

struct Priority{T}
    dist::Float64
    item::Union{T, Nothing}
end

function nearest(tree::Tree{T, C}, x::C, y::C, m::Int) where {T, C<:Real}
    if tree.root === nothing || m <= 0
        return Priority{T}[]
    end

    list = [Priority{T}(Inf, nothing) for _ in 1:m]
    xq_box = (x, y, x, y)
    bp = [typemax(C) for _ in 1:4]
    bn = [typemin(C) for _ in 1:4]

    kd_neighbor!(tree.root, xq_box, m, list, bp, bn)

    return [Priority{T}(sqrt(p.dist), p.item) for p in list]
end

function kd_neighbor!(node::Node{T, C}, xq::Box{C}, m::Int, list::Vector{Priority{T}}, bp::Vector{C}, bn::Vector{C}) where {T, C<:Real}
    stack = Tuple{Node{T, C}, Int, Int, Vector{C}, Vector{C}}[]
    push!(stack, (node, 1, 0, copy(bn), copy(bp)))

    while !isempty(stack)
        curr_node, d, state, cur_bn, cur_bp = pop!(stack)
        p = curr_node.size[d]
        hort = ((d - 1) & 1) + 1
        vert = d > 2

        if state == 0
            if curr_node.item !== nothing
                add_priority!(m, list, xq, curr_node)
            end
            push!(stack, (curr_node, d, 1, cur_bn, cur_bp))
        elseif state == 1
            push!(stack, (curr_node, d, 2, cur_bn, cur_bp))
            if xq[d] <= p
                if curr_node.sons[1] !== nothing
                    old_bn = cur_bn[hort]
                    old_bp = cur_bp[hort]
                    if vert
                        cur_bp[hort] = curr_node.size[d]
                        cur_bn[hort] = curr_node.lo_min_bound
                    else
                        cur_bp[hort] = curr_node.other_bound
                        cur_bn[hort] = curr_node.lo_min_bound
                    end
                    if bounds_overlap_ball(xq, cur_bp, cur_bn, m, list)
                        push!(stack, (curr_node.sons[1], next_disc(d), 0, copy(cur_bn), copy(cur_bp)))
                    end
                    cur_bn[hort] = old_bn
                    cur_bp[hort] = old_bp
                end
            else
                if curr_node.sons[2] !== nothing
                    old_bn = cur_bn[hort]
                    old_bp = cur_bp[hort]
                    if vert
                        cur_bp[hort] = curr_node.hi_max_bound
                        cur_bn[hort] = curr_node.other_bound
                    else
                        cur_bp[hort] = curr_node.hi_max_bound
                        cur_bn[hort] = curr_node.size[d]
                    end
                    if bounds_overlap_ball(xq, cur_bp, cur_bn, m, list)
                        push!(stack, (curr_node.sons[2], next_disc(d), 0, copy(cur_bn), copy(cur_bp)))
                    end
                    cur_bn[hort] = old_bn
                    cur_bp[hort] = old_bp
                end
            end
        elseif state == 2
            if xq[d] <= p
                if curr_node.sons[2] !== nothing
                    old_bn = cur_bn[hort]
                    old_bp = cur_bp[hort]
                    if vert
                        cur_bp[hort] = curr_node.hi_max_bound
                        cur_bn[hort] = curr_node.other_bound
                    else
                        cur_bp[hort] = curr_node.hi_max_bound
                        cur_bn[hort] = curr_node.size[d]
                    end
                    if bounds_overlap_ball(xq, cur_bp, cur_bn, m, list)
                        push!(stack, (curr_node.sons[2], next_disc(d), 0, copy(cur_bn), copy(cur_bp)))
                    end
                    cur_bn[hort] = old_bn
                    cur_bp[hort] = old_bp
                end
            else
                if curr_node.sons[1] !== nothing
                    old_bn = cur_bn[hort]
                    old_bp = cur_bp[hort]
                    if vert
                        cur_bp[hort] = curr_node.size[d]
                        cur_bn[hort] = curr_node.lo_min_bound
                    else
                        cur_bp[hort] = curr_node.other_bound
                        cur_bn[hort] = curr_node.lo_min_bound
                    end
                    if bounds_overlap_ball(xq, cur_bp, cur_bn, m, list)
                        push!(stack, (curr_node.sons[1], next_disc(d), 0, copy(cur_bn), copy(cur_bp)))
                    end
                    cur_bn[hort] = old_bn
                    cur_bp[hort] = old_bp
                end
            end
        end
    end
end

function add_priority!(m::Int, list::Vector{Priority{T}}, xq::Box{C}, node::Node{T, C}) where {T, C<:Real}
    d = kd_dist_sq(xq, node.size)
    for x in m:-1:1
        if d < list[x].dist
            if x != m
                list[x+1] = list[x]
            end
            list[x] = Priority{T}(d, node.item)
        else
            break
        end
    end
end

function bounds_overlap_ball(xq::Box{C}, bp::Vector{C}, bn::Vector{C}, m::Int, list::Vector{Priority{T}}) where {T, C<:Real}
    sum_dist = 0.0
    max_dist = list[m].dist
    for i in 1:2
        if xq[i] < bn[i]
            d = Float64(xq[i] - bn[i])
            sum_dist += d * d
            if sum_dist > max_dist
                return false
            end
        elseif xq[i] > bp[i]
            d = Float64(xq[i] - bp[i])
            sum_dist += d * d
            if sum_dist > max_dist
                return false
            end
        end
    end
    return true
end

function kd_dist_sq(xq::Box{C}, box_size::Box{C}) where {C<:Real}
    dx = 0.0
    dy = 0.0

    if xq[LEFT] > box_size[RIGHT]
        dx = Float64(xq[LEFT] - box_size[RIGHT])
    elseif xq[RIGHT] < box_size[LEFT]
        dx = Float64(box_size[LEFT] - xq[RIGHT])
    end

    if xq[BOTTOM] > box_size[TOP]
        dy = Float64(xq[BOTTOM] - box_size[TOP])
    elseif xq[TOP] < box_size[BOTTOM]
        dy = Float64(box_size[BOTTOM] - xq[TOP])
    end

    return dx*dx + dy*dy
end


function serialize(tree::Tree{T, C}, filename::String, item_to_id::Function) where {T, C<:Real}
    count = tree.item_count - tree.dead_count
    if count <= 0
        error("Empty tree")
    end

    node_size = 8 + 4*sizeof(C) + 3*sizeof(C) + 16

    io = open(filename, "w+")
    truncate(io, count * node_size)

    current_idx = 0
    function serialize_node(node::Union{Node{T, C}, Nothing})
        if node === nothing || node.item === nothing
            return Int64(-1)
        end
        my_idx = current_idx
        current_idx += 1

        left = serialize_node(node.sons[1])
        right = serialize_node(node.sons[2])

        seek(io, my_idx * node_size)
        write(io, UInt64(item_to_id(node.item)))
        for i in 1:4
            write(io, node.size[i])
        end
        write(io, node.lo_min_bound)
        write(io, node.hi_max_bound)
        write(io, node.other_bound)
        write(io, Int64(left))
        write(io, Int64(right))

        return Int64(my_idx)
    end

    serialize_node(tree.root)
    close(io)
end

function get_bounds(tree::Tree{T, C})::Union{Box{C}, Nothing} where {T, C<:Real}
    if tree.root === nothing
        return nothing
    end
    
    bounds = [tree.root.size...]
    dim = length(bounds) ÷ 2
    
    function traverse(node::Union{Node{T, C}, Nothing})
        if node === nothing
            return
        end
        if node.item !== nothing
            for d in 1:dim
                if node.size[d] < bounds[d]
                    bounds[d] = node.size[d]
                end
                if node.size[d + dim] > bounds[d + dim]
                    bounds[d + dim] = node.size[d + dim]
                end
            end
        end
        traverse(node.sons[1])
        traverse(node.sons[2])
    end
    
    traverse(tree.root)
    return Box{C}((bounds...,))
end

function get_mmap_bounds(nodes::Vector{MmapNode{N, C}}, dim::Int=2)::Union{Box{C}, Nothing} where {N, C<:Real}
    if isempty(nodes)
        return nothing
    end
    
    bounds = nothing
    for node in nodes
        if node.source_id != 0
            if bounds === nothing
                bounds = [node.size...]
            else
                for d in 1:dim
                    if node.size[d] < bounds[d]
                        bounds[d] = node.size[d]
                    end
                    if node.size[d + dim] > bounds[d + dim]
                        bounds[d + dim] = node.size[d + dim]
                    end
                end
            end
        end
    end
    
    if bounds === nothing
        return nothing
    end
    return NTuple{2*dim, C}((bounds...,))
end

function get_serialized_bounds(filename::String, CType::Type{C}, dim::Int=2)::Union{Box{C}, Nothing} where {C<:Real}
    if !isfile(filename)
        return nothing
    end
    
    node_size = 8 + 2*dim*sizeof(C) + 3*sizeof(C) + 16
    file_size = filesize(filename)
    if file_size == 0 || file_size % node_size != 0
        return nothing
    end
    
    node_count = file_size ÷ node_size
    
    # Try O(1) fast sentinel check at the end of the file
    io = open(filename, "r")
    try
        seek(io, (node_count - 1) * node_size)
        source_id = read(io, UInt64)
        if source_id == typemax(UInt64)
            size_tup = NTuple{2*dim, C}(Tuple(read(io, CType) for _ in 1:(2*dim)))
            return size_tup
        end
    catch e
        # Ignore and fallback
    finally
        close(io)
    end
    
    # Fallback to O(N) scan
    nodes = Vector{MmapNode{2*dim, C}}(undef, node_count)
    io = open(filename, "r")
    try
        for i in 1:node_count
            seek(io, (i - 1) * node_size)
            source_id = read(io, UInt64)
            size_tup = NTuple{2*dim, C}(Tuple(read(io, CType) for _ in 1:(2*dim)))
            lo_min_bound = read(io, CType)
            hi_max_bound = read(io, CType)
            other_bound = read(io, CType)
            left_child = read(io, Int64)
            right_child = read(io, Int64)
            
            nodes[i] = MmapNode{2*dim, C}(
                source_id,
                size_tup,
                lo_min_bound,
                hi_max_bound,
                other_bound,
                left_child,
                right_child
            )
        end
    finally
        close(io)
    end
    
    return get_mmap_bounds(nodes, dim)
end

end
