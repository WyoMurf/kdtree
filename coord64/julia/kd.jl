module KD

export Tree, Box, insert!, count_items, is_member, hard_delete!, search, LEFT, BOTTOM, RIGHT, TOP

const Box = NTuple{4, Int64}
const LEFT = 1
const BOTTOM = 2
const RIGHT = 3
const TOP = 4

mutable struct Node{T}
    item::Union{T, Nothing}
    size::Box
    lo_min_bound::Int64
    hi_max_bound::Int64
    other_bound::Int64
    sons::Vector{Union{Node{T}, Nothing}}
    
    function Node{T}(item, size::Box, lo::Int, hi::Int, ob::Int) where T
        new{T}(item, size, lo, hi, ob, Union{Node{T}, Nothing}[nothing, nothing])
    end
end

mutable struct Tree{T}
    root::Union{Node{T}, Nothing}
    item_count::Int
    dead_count::Int
    extent::Vector{Int64}
    
    function Tree{T}() where T
        new{T}(nothing, 0, 0, [0, 0, 0, 0])
    end
end

next_disc(disc::Int) = (disc % 4) + 1

function bounds_update!(node::Node{T}, disc::Int, size::Box) where T
    vert = ((disc - 1) & 1) + 1
    node.lo_min_bound = min(node.lo_min_bound, size[vert])
    node.hi_max_bound = max(node.hi_max_bound, size[vert + 2])
    if ((disc - 1) & 2) != 0
        node.other_bound = min(node.other_bound, size[vert])
    else
        node.other_bound = max(node.other_bound, size[vert + 2])
    end
end

function insert!(tree::Tree{T}, item::T, size::Box) where T
    if tree.root === nothing
        tree.root = Node{T}(item, size, size[LEFT], size[RIGHT], size[LEFT])
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

function insert_recursive!(elem::Node{T}, disc::Int, item::T, size::Box) where T
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

    new_node = Node{T}(item, size, lo, hi, ob)
    elem.sons[child_idx] = new_node
    bounds_update!(elem, disc, size)
    return true
end

function intersect(b1::Box, b2::Box)
    return b1[RIGHT] >= b2[LEFT] &&
           b2[RIGHT] >= b1[LEFT] &&
           b1[TOP] >= b2[BOTTOM] &&
           b2[TOP] >= b1[BOTTOM]
end

function search(tree::Tree{T}, extent::Box) where T
    results = Vector{Tuple{T, Box}}()
    if tree.root === nothing
        return results
    end
    
    stack = Tuple{Node{T}, Int, Int}[]
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

function hard_delete!(tree::Tree{T}, item::T, size::Box) where T
    initial_count = tree.item_count
    tree.root = hard_delete_recursive!(tree.root, 1, item, size, tree)
    return tree.item_count < initial_count
end

function hard_delete_recursive!(node::Union{Node{T}, Nothing}, disc::Int, item::T, size::Box, tree::Tree{T}) where T
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

function find_extreme(node::Node{T}, node_disc::Int, target_disc::Int, find_min::Bool) where T
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

function find_recursive(elem::Union{Node{T}, Nothing}, disc::Int, item::T, size::Box) where T
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

is_member(tree::Tree{T}, item::T, size::Box) where T = find_recursive(tree.root, 1, item, size)
count_items(tree::Tree{T}) where T = tree.item_count - tree.dead_count

end
