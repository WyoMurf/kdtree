using Test
include("kd.jl")
using .KD
using Random

@testset "KDTree Basic & Hard Delete" begin
    tree = Tree{String}()
    box1 = (0, 0, 10, 10)
    box2 = (20, 20, 30, 30)
    box3 = (5, 5, 15, 15)

    KD.insert!(tree, "item1", box1)
    KD.insert!(tree, "item2", box2)
    KD.insert!(tree, "item3", box3)

    @test count_items(tree) == 3
    @test is_member(tree, "item2", box2)

    found = search(tree, (0, 0, 15, 15))
    @test length(found) == 2
    items = [f[1] for f in found]
    @test "item1" in items
    @test "item3" in items

    @test hard_delete!(tree, "item1", box1) == true
    @test count_items(tree) == 2
    @test !is_member(tree, "item1", box1)
    @test is_member(tree, "item3", box3)
end

@testset "Million Boxes Stress Test" begin
    tree = Tree{String}()
    Random.seed!(42)
    
    boxes_to_delete = Tuple{String, Box}[]
    
    # 1. Insert 1,000,000 Boxes
    for i in 1:1_000_000
        x1 = rand(0:99999)
        y1 = rand(0:99999)
        x2 = x1 + rand(1:100)
        y2 = y1 + rand(1:100)
        b = (x1, y1, x2, y2)
        
        name = "box$i"
        if i <= 1000
            push!(boxes_to_delete, (name, b))
        end
        KD.insert!(tree, name, b)
    end
    
    @test count_items(tree) == 1_000_000
    
    # 2. Search a Quarter Sector
    found = search(tree, (0, 0, 50000, 50000))
    println("Found $(length(found)) boxes in the 0-50000 search area")
    @test length(found) > 10000 # Sanity check for data population
    
    # 3. Hard Delete 1,000 Items Deeply Nested in Structure
    for (name, b) in boxes_to_delete
        success = hard_delete!(tree, name, b)
        @test success
    end
    
    @test count_items(tree) == 999_000
end

@testset "KDTree Really Delete & Badness & Nearest" begin
    tree = Tree{Int64}()
    Random.seed!(42)
    
    # Generate 10,000 random boxes
    boxes = Box[]
    for i in 1:10000
        x1 = rand(-100000:100000)
        y1 = rand(-100000:100000)
        x2 = x1 + rand(1:1000)
        y2 = y1 + rand(1:1000)
        b = (x1, y1, x2, y2)
        push!(boxes, b)
        KD.insert!(tree, i, b)
    end
    
    @test count_items(tree) == 10000
    
    # 1. Test badness
    badness(tree) # Just run and ensure no errors
    
    # 2. Test nearest search
    for m in [1, 2, 4, 8, 16, 20]
        for q in 1:50
            qx = rand(-100000:100000)
            qy = rand(-100000:100000)
            
            list = nearest(tree, qx, qy, m)
            @test length(list) == m
            
            # Verify sorted by distance non-decreasing
            for idx in 2:m
                @test list[idx].dist >= list[idx-1].dist - 1e-9
            end
            
            # Brute-force verification
            brute_dists = Float64[]
            for box in boxes
                # Calculate the exact distance using our distance function
                dx = 0.0
                dy = 0.0
                if qx > box[RIGHT]
                    dx = Float64(qx - box[RIGHT])
                elseif qx < box[LEFT]
                    dx = Float64(box[LEFT] - qx)
                end
                if qy > box[TOP]
                    dy = Float64(qy - box[TOP])
                elseif qy < box[BOTTOM]
                    dy = Float64(box[BOTTOM] - qy)
                end
                push!(brute_dists, sqrt(dx*dx + dy*dy))
            end
            sort!(brute_dists)
            
            # kd furthest element must be extremely close to brute m-th closest element
            @test list[m].dist <= brute_dists[m] + 1e-6
        end
    end
    
    # Edge case: point inside box
    qx = (boxes[1][LEFT] + boxes[1][RIGHT]) ÷ 2
    qy = (boxes[1][BOTTOM] + boxes[1][TOP]) ÷ 2
    list = nearest(tree, qx, qy, 1)
    @test list[1].dist <= 1e-9
    
    # 3. Test really_delete!
    for i in 10000:-1:1
        status, _, _ = really_delete!(tree, i, boxes[i])
        @test status == 1
    end
    
    @test count_items(tree) == 0
    
    # Verify empty
    @test length(search(tree, (-100001, -100001, 100001, 100001))) == 0
end


@testset "Million Boxes 64-bit Stress Test" begin
    tree = Tree{String}()
    Random.seed!(42)
    
    boxes_to_delete = Tuple{String, Box}[]
    
    for i in 1:1_000_000
        x1 = rand(0:10_000_000_000)
        y1 = rand(0:10_000_000_000)
        x2 = x1 + rand(1:100)
        y2 = y1 + rand(1:100)
        b = (x1, y1, x2, y2)
        
        name = "box$i"
        if i <= 1000
            push!(boxes_to_delete, (name, b))
        end
        KD.insert!(tree, name, b)
    end
    
    @test count_items(tree) == 1_000_000
    
    found = search(tree, (0, 0, 5_000_000_000, 5_000_000_000))
    println("Found $(length(found)) boxes in the 0-5,000,000,000 search area")
    @test length(found) > 10000
    
    for (name, b) in boxes_to_delete
        success = hard_delete!(tree, name, b)
        @test success
    end
    
    @test count_items(tree) == 999_000
end
