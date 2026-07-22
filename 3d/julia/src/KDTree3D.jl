module KDTree3D

export serialize, get_bounds, get_serialized_bounds, get_mmap_bounds, MmapNode, Tree, Box, insert!, delete!, count_items, is_member, hard_delete!, search, nearest, 
       LEFT, BOTTOM, FLOOR, RIGHT, TOP, CEIL, build_tree, rebuild!, badness

const Box{C<:Integer} = NTuple{6, C}
const LEFT = 1
const BOTTOM = 2
const FLOOR = 3
const RIGHT = 4
const TOP = 5
const CEIL = 6

struct MmapNode{N, C<:Integer}
    source_id::UInt64
    size::NTuple{N, C}
    lo_min_bound::C
    hi_max_bound::C
    other_bound::C
    left_child::Int64
    right_child::Int64
end

mutable struct Node{T, C<:Integer}
    item::Union{T, Nothing}
    size::Box{C}
    lo_min_bound::C
    hi_max_bound::C
    other_bound::C
    sons::Vector{Union{Node{T, C}, Nothing}}
    
    function Node{T, C}(item, size::Box{C}, lo::C, hi::C, ob::C) where {T, C<:Integer}
        new{T, C}(item, size, lo, hi, ob, Union{Node{T, C}, Nothing}[nothing, nothing])
    end
end

mutable struct Tree{T, C<:Integer}
    root::Union{Node{T, C}, Nothing}
    item_count::Int
    dead_count::Int
    extent::Vector{C}
    
    function Tree{T, C}() where {T, C<:Integer}
        new{T, C}(nothing, 0, 0, zeros(C, 6))
    end
end

next_disc(disc::Int) = (disc % 6) + 1

function bounds_update!(node::Node{T, C}, disc::Int, size::Box{C}) where {T, C<:Integer}
    vert = ((disc - 1) % 3) + 1
    node.lo_min_bound = min(node.lo_min_bound, size[vert])
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 3])
    if disc > 3
        node.other_bound = min(node.other_bound, size[vert])
    else
        node.other_bound = max(node.other_bound, size[vert + 3])
    end
end

function insert!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Integer}
    if tree.root === nothing
        tree.root = Node{T, C}(item, size, size[1], size[4], size[1])
        tree.extent = collect(size)
        tree.item_count = 1
        return
    end

    if insert_recursive!(tree.root, 1, item, size)
        tree.item_count += 1
        for i in 1:3
            tree.extent[i] = min(tree.extent[i], size[i])
            tree.extent[i+3] = max(tree.extent[i+3], size[i+3])
        end
    end
end

function insert_recursive!(elem::Node{T, C}, disc::Int, item::T, size::Box{C}) where {T, C<:Integer}
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

    vert = ((next_disc(disc) - 1) % 3) + 1
    lo = size[vert]
    hi = size[vert + 3]
    ob = (next_disc(disc) > 3) ? size[vert] : size[vert + 3]

    new_node = Node{T, C}(item, size, lo, hi, ob)
    elem.sons[child_idx] = new_node
    bounds_update!(elem, disc, size)
    return true
end

function intersect(b1::Box{C}, b2::Box{C}) where {C<:Integer}
    return b1[RIGHT] >= b2[LEFT] &&
           b2[RIGHT] >= b1[LEFT] &&
           b1[TOP] >= b2[BOTTOM] &&
           b2[TOP] >= b1[BOTTOM] &&
           b1[CEIL] >= b2[FLOOR] &&
           b2[CEIL] >= b1[FLOOR]
end

function search(tree::Tree{T, C}, extent::Box{C}) where {T, C<:Integer}
    results = Vector{Tuple{T, Box{C}}}()
    if tree.root === nothing
        return results
    end
    
    stack = Tuple{Node{T, C}, Int, Int}[]
    push!(stack, (tree.root, 1, 0))
    
    while !isempty(stack)
        node, disc, state = pop!(stack)
        hort = ((disc - 1) % 3) + 1
        
        if state == 0
            push!(stack, (node, disc, 1))
            if node.item !== nothing && intersect(extent, node.size)
                push!(results, (node.item, node.size))
            end
        elseif state == 1
            push!(stack, (node, disc, 2))
            if node.sons[1] !== nothing
                should_push = false
                if disc > 3
                    if extent[hort] <= node.size[disc] && extent[hort+3] >= node.lo_min_bound
                        should_push = true
                    end
                else
                    if extent[hort] <= node.other_bound && extent[hort+3] >= node.lo_min_bound
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
                if disc > 3
                    if extent[hort] <= node.hi_max_bound && extent[hort+3] >= node.other_bound
                        should_push = true
                    end
                else
                    if extent[hort] <= node.hi_max_bound && extent[hort+3] >= node.size[disc]
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

function hard_delete!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Integer}
    initial_count = tree.item_count
    tree.root = hard_delete_recursive!(tree.root, 1, item, size, tree)
    return tree.item_count < initial_count
end

function hard_delete_recursive!(node::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}, tree::Tree{T, C}) where {T, C<:Integer}
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
    if val == 0
        ndisc = next_disc(disc)
        while ndisc != disc
            val = size[ndisc] - node.size[ndisc]
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
    node.sons[child_idx] = hard_delete_recursive!(node.sons[child_idx], next_disc(disc), item, size, tree)
    return node
end

function find_extreme(node::Node{T, C}, node_disc::Int, target_disc::Int, find_min::Bool) where {T, C<:Integer}
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

function find_recursive(elem::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}) where {T, C<:Integer}
    if elem === nothing
        return false
    end
    if elem.item !== nothing && elem.item == item
        return true
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
    return find_recursive(elem.sons[child_idx], next_disc(disc), item, size)
end

is_member(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Integer} = find_recursive(tree.root, 1, item, size)
count_items(tree::Tree{T, C}) where {T, C<:Integer} = tree.item_count - tree.dead_count

struct Priority{T}
    dist::Float64
    item::Union{T, Nothing}
end

function nearest(tree::Tree{T, C}, x::C, y::C, z::C, m::Int) where {T, C<:Integer}
    if tree.root === nothing || m <= 0
        return Priority{T}[]
    end

    list = [Priority{T}(Inf, nothing) for _ in 1:m]
    xq = (x, y, z, x, y, z)
    bp = [typemax(C) for _ in 1:6]
    bn = [typemin(C) for _ in 1:6]

    kd_neighbor!(tree.root, xq, m, list, bp, bn)

    # Convert squared distances to actual distances
    return [Priority{T}(sqrt(p.dist), p.item) for p in list]
end

function kd_neighbor!(node::Node{T, C}, xq::Box{C}, m::Int, list::Vector{Priority{T}}, bp::Vector{C}, bn::Vector{C}) where {T, C<:Integer}
    stack = Tuple{Node{T, C}, Int, Int, Vector{C}, Vector{C}}[]
    push!(stack, (node, 1, 0, copy(bn), copy(bp)))

    while !isempty(stack)
        curr_node, d, state, cur_bn, cur_bp = pop!(stack)
        p = curr_node.size[d]
        hort = ((d - 1) % 3) + 1
        vert = d > 3

        if state == 0 # THIS_ONE
            if curr_node.item !== nothing
                add_priority!(m, list, xq, curr_node)
            end
            push!(stack, (curr_node, d, 1, cur_bn, cur_bp))
        elseif state == 1 # LOSON (or first side)
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
        elseif state == 2 # HISON (or second side)
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

function add_priority!(m::Int, list::Vector{Priority{T}}, xq::Box{C}, node::Node{T, C}) where {T, C<:Integer}
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

function bounds_overlap_ball(xq::Box{C}, bp::Vector{C}, bn::Vector{C}, m::Int, list::Vector{Priority{T}}) where {T, C<:Integer}
    sum_dist = 0.0
    max_dist = list[m].dist
    for i in 1:3
        if xq[i] < bn[i]
            d = Float64(xq[i] - bn[i])
            sum_dist += d * d
            if sum_dist > max_dist
                return false
            end
        elseif xq[i+3] > bp[i]
            d = Float64(xq[i] - bp[i])
            sum_dist += d * d
            if sum_dist > max_dist
                return false
            end
        end
    end
    return true
end

function kd_dist_sq(xq::Box{C}, box_size::Box{C}) where {C<:Integer}
    dx = 0.0
    dy = 0.0
    dz = 0.0

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

    if xq[FLOOR] > box_size[CEIL]
        dz = Float64(xq[FLOOR] - box_size[CEIL])
    elseif xq[CEIL] < box_size[FLOOR]
        dz = Float64(box_size[FLOOR] - xq[CEIL])
    end

    return dx*dx + dy*dy + dz*dz
end

function find_item(node::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}) where {T, C<:Integer}
    path = Node{T, C}[]
    curr = node
    d = disc
    while curr !== nothing
        if curr.item !== nothing && curr.item == item
            return curr, path
        end
        
        val = size[d] - curr.size[d]
        if val == 0
            ndisc = next_disc(d)
            while ndisc != d
                val = size[ndisc] - curr.size[ndisc]
                if val != 0
                    break
                end
                ndisc = next_disc(ndisc)
            end
            if val == 0
                val = 1
            end
        end
        
        push!(path, curr)
        child_idx = val >= 0 ? 2 : 1
        curr = curr.sons[child_idx]
        d = next_disc(d)
    end
    return nothing, path
end

function del_element!(tree::Tree{T, C}, node::Node{T, C}, path::Vector{Node{T, C}}) where {T, C<:Integer}
    if node.item === nothing
        if node.sons[1] === nothing && node.sons[2] === nothing
            if !isempty(path)
                parent = path[end]
                if parent.sons[1] === node
                    parent.sons[1] = nothing
                elseif parent.sons[2] === node
                    parent.sons[2] = nothing
                end
                tree.dead_count -= 1
                tree.item_count -= 1
                # Recurse on parent
                pop!(path)
                del_element!(tree, parent, path)
            else
                tree.root = nothing
                tree.dead_count -= 1
                tree.item_count -= 1
            end
        end
    end
end

function delete!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Integer}
    node, path = find_item(tree.root, 1, item, size)
    if node !== nothing
        node.item = nothing
        tree.dead_count += 1
        del_element!(tree, node, path)
        return true
    end
    return false
end

function get_min_max_bounds(nodes::Vector{Tuple{T, Box{C}}}, median_node::Tuple{T, Box{C}}, disc::Int) where {T, C<:Integer}
    vert = ((disc - 1) % 3) + 1
    b_min = median_node[2][vert]
    b_max = median_node[2][vert + 3]
    
    for node in nodes
        b_min = min(b_min, node[2][vert])
        b_max = max(b_max, node[2][vert + 3])
    end
    return b_min, b_max
end

function build_node_recursive!(nodes::Vector{Tuple{T, Box{C}}}, disc::Int, level::Int, max_level::Int, mean::Float64) where {T, C<:Integer}
    num = length(nodes)
    if num == 0
        return nothing, 0
    end

    # Find item closest to mean at disc
    best_dist = Inf
    best_idx = 1
    for i in 1:num
        dist = abs(nodes[i][2][disc] - mean)
        if dist < best_dist
            best_dist = dist
            best_idx = i
        end
    end

    median_node = nodes[best_idx]
    median_val = median_node[2][disc]

    # Partition
    lo = Tuple{T, Box{C}}[]
    eq = Tuple{T, Box{C}}[]
    hi = Tuple{T, Box{C}}[]
    lomean = 0.0
    himean = 0.0
    
    for i in 1:num
        node = nodes[i]
        val = node[2][disc]
        if val < median_val
            push!(lo, node)
            lomean += node[2][next_disc(disc)]
        elseif val > median_val
            push!(hi, node)
            himean += node[2][next_disc(disc)]
        else
            push!(eq, node)
        end
    end

    # Find median_node in eq and remove it
    for i in 1:length(eq)
        if eq[i] == median_node
            deleteat!(eq, i)
            break
        end
    end
    
    # Reclassify remaining eq
    while !isempty(eq)
        other = popfirst!(eq)
        val = 0
        cur_disc = next_disc(disc)
        while cur_disc != disc
            val = other[2][cur_disc] - median_node[2][cur_disc]
            if val != 0
                break
            end
            cur_disc = next_disc(cur_disc)
        end
        
        if val < 0
            push!(lo, other)
            lomean += other[2][next_disc(disc)]
        else
            push!(hi, other)
            himean += other[2][next_disc(disc)]
        end
    end

    lo_min_bound, lo_max_bound = get_min_max_bounds(lo, median_node, disc)
    hi_min_bound, hi_max_bound = get_min_max_bounds(hi, median_node, disc)

    node = Node{T, C}(median_node[1], median_node[2], lo_min_bound, hi_max_bound, (disc > 3 ? lo_min_bound : hi_max_bound))

    count = 1
    if level < max_level
        if !isempty(lo)
            node.sons[1], c = build_node_recursive!(lo, next_disc(disc), level+1, max_level, lomean/length(lo))
            count += c
        end
        if !isempty(hi)
            node.sons[2], c = build_node_recursive!(hi, next_disc(disc), level+1, max_level, himean/length(hi))
            count += c
        end
    end
    
    return node, count
end

function build_tree(items::Vector{T}, boxes::Vector{Box{C}}, max_level::Int=100000) where {T, C<:Integer}
    num = length(items)
    if num == 0
        return Tree{T, C}()
    end

    extent = [typemax(C) for _ in 1:6]
    mean = 0.0
    for i in 1:num
        b = boxes[i]
        for j in 1:3
            extent[j] = min(extent[j], b[j])
            extent[j+3] = max(extent[j+3], b[j+3])
        end
        mean += b[1]
    end
    mean /= num

    nodes = Tuple{T, Box{C}}[(items[i], boxes[i]) for i in 1:num]
    
    tree = Tree{T, C}()
    tree.extent = extent
    root, count = build_node_recursive!(nodes, 1, 1, max_level, mean)
    tree.root = root
    tree.item_count = count
    return tree
end

function unload_items!(node::Union{Node{T, C}, Nothing}, items::Vector{T}, boxes::Vector{Box{C}}) where {T, C<:Integer}
    if node === nothing
        return
    end
    if node.item !== nothing
        push!(items, node.item)
        push!(boxes, node.size)
    end
    unload_items!(node.sons[1], items, boxes)
    unload_items!(node.sons[2], items, boxes)
end

function rebuild!(tree::Tree{T, C}) where {T, C<:Integer}
    items = T[]
    boxes = Box{C}[]
    unload_items!(tree.root, items, boxes)
    new_tree = build_tree(items, boxes)
    tree.root = new_tree.root
    tree.item_count = new_tree.item_count
    tree.dead_count = 0
    tree.extent = new_tree.extent
end

function badness(tree::Tree{T, C}) where {T, C<:Integer}
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

function node_cmp(a::Node{T, C}, b::Node{T, C}, disc::Int) where {T, C<:Integer}
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

struct FindSave{T, C<:Integer}
    node::Node{T, C}
    disc::Int
    state::Ref{Int}
end

function find_min_max_node!(t::Tree{T, C}, j::Int, kd_minval_node::Ref{Node{T, C}}, kd_minval_nodesdad::Ref{Node{T, C}}, dir::Ref{Int}, newj::Ref{Int}) where {T, C<:Integer}
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

function find_item_with_path(node::Union{Node{T, C}, Nothing}, disc::Int, item::T, size::Box{C}, path::Vector{Node{T, C}}) where {T, C<:Integer}
    if node === nothing
        return nothing, path
    end
    if node.item !== nothing && node.item == item
        return node, path
    end

    val = size[disc] - node.size[disc]
    if val == 0
        ndisc = next_disc(disc)
        while ndisc != disc
            val = size[ndisc] - node.size[ndisc]
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

    if node.sons[child_idx] !== nothing
        new_path = copy(path)
        push!(new_path, node)
        return find_item_with_path(node.sons[child_idx], next_disc(disc), item, size, new_path)
    end

    return nothing, path
end

function kd_do_delete!(t::Tree{T, C}, elem::Node{T, C}, j::Int, stats::DeleteStats) where {T, C<:Integer}
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

function really_delete!(tree::Tree{T, C}, item::T, old_size::Box{C}) where {T, C<:Integer}
    elem, path = find_item_with_path(tree.root, 1, item, old_size, Node{T, C}[])
    if elem === nothing
        return (-4, 0, 0)
    end

    stats = DeleteStats(0, 1)

    if elem === tree.root
        tree.root = kd_do_delete!(tree, elem, 1, stats)
    else
        parent = path[end]
        j = (length(path) % 6) + 1
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


function serialize(tree::Tree{T, C}, filename::String, item_to_id::Function) where {T, C<:Integer}
    count = tree.item_count - tree.dead_count
    if count <= 0
        error("Empty tree")
    end

    node_size = 8 + 6*sizeof(C) + 3*sizeof(C) + 16

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
        for i in 1:6
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

function get_bounds(tree::Tree{T, C})::Union{Box{C}, Nothing} where {T, C<:Integer}
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

function get_mmap_bounds(nodes::Vector{MmapNode{N, C}}, dim::Int=3)::Union{Box{C}, Nothing} where {N, C<:Integer}
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

function get_serialized_bounds(filename::String, CType::Type{C}, dim::Int=3)::Union{Box{C}, Nothing} where {C<:Integer}
    if !isfile(filename)
        return nothing
    end
    
    node_size = 8 + 2*dim*sizeof(C) + 3*sizeof(C) + 16
    file_size = filesize(filename)
    if file_size == 0 || file_size % node_size != 0
        return nothing
    end
    
    node_count = file_size ÷ node_size
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
