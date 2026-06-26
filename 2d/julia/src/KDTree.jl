module KDTree

export Tree, Box, insert!, count_items, is_member, hard_delete!, really_delete!, badness, nearest, Priority, search, LEFT, BOTTOM, RIGHT, TOP

const Box{C<:Integer} = NTuple{4, C}
const LEFT = 1
const BOTTOM = 2
const RIGHT = 3
const TOP = 4

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
        new{T, C}(nothing, 0, 0, zeros(C, 4))
    end
end

next_disc(disc::Int) = (disc % 4) + 1

function bounds_update!(node::Node{T, C}, disc::Int, size::Box{C}) where {T, C<:Integer}
    vert = ((disc - 1) & 1) + 1
    node.lo_min_bound = min(node.lo_min_bound, size[vert])
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 2])
    if ((disc - 1) & 2) != 0
        node.other_bound = min(node.other_bound, size[vert])
    else
        node.other_bound = max(node.other_bound, size[vert + 2])
    end
end

function insert!(tree::Tree{T, C}, item::T, size::Box{C}) where {T, C<:Integer}
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

    vert = ((next_disc(disc) - 1) & 1) + 1
    lo = size[vert]
    hi = size[vert + 2]
    ob = (((next_disc(disc) - 1) & 2) != 0) ? size[vert] : size[vert + 2]

    new_node = Node{T, C}(item, size, lo, hi, ob)
    elem.sons[child_idx] = new_node
    bounds_update!(elem, disc, size)
    return true
end

function intersect(b1::Box{C}, b2::Box{C}) where {C<:Integer}
    return b1[RIGHT] >= b2[LEFT] &&
           b2[RIGHT] >= b1[LEFT] &&
           b1[TOP] >= b2[BOTTOM] &&
           b2[TOP] >= b1[BOTTOM]
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

struct Priority{T}
    dist::Float64
    item::Union{T, Nothing}
end

function nearest(tree::Tree{T, C}, x::C, y::C, m::Int) where {T, C<:Integer}
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

function kd_neighbor!(node::Node{T, C}, xq::Box{C}, m::Int, list::Vector{Priority{T}}, bp::Vector{C}, bn::Vector{C}) where {T, C<:Integer}
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

function kd_dist_sq(xq::Box{C}, box_size::Box{C}) where {C<:Integer}
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

end
