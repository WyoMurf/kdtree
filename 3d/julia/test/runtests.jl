using KDTree3D
using Test
import KDTree3D: insert!, search, nearest, count_items, is_member, hard_delete!, delete!, build_tree, rebuild!, badness

@testset "KDTree3D basic tests" begin
    tree = Tree{Int32, Int32}()
    
    # Insert some items
    boxes = [
        (Int32(0), Int32(0), Int32(0), Int32(10), Int32(10), Int32(10)),
        (Int32(20), Int32(20), Int32(20), Int32(30), Int32(30), Int32(30)),
        (Int32(10), Int32(10), Int32(10), Int32(25), Int32(25), Int32(25))
    ]
    
    for (i, box) in enumerate(boxes)
        insert!(tree, Int32(i), box)
    end
    
    @test count_items(tree) == 3
    
    @test is_member(tree, Int32(1), boxes[1])
    @test is_member(tree, Int32(2), boxes[2])
    @test is_member(tree, Int32(3), boxes[3])
    @test !is_member(tree, Int32(4), (Int32(0),Int32(0),Int32(0),Int32(0),Int32(0),Int32(0)))
    
    # Search
    results = search(tree, (Int32(5), Int32(5), Int32(5), Int32(15), Int32(15), Int32(15)))
    @test length(results) == 2
    items = [r[1] for r in results]
    @test 1 in items
    @test 3 in items
    @test !(2 in items)
    
    # Hard delete
    @test hard_delete!(tree, Int32(1), boxes[1])
    @test count_items(tree) == 2
    @test !is_member(tree, Int32(1), boxes[1])
    
    # Soft delete
    @test delete!(tree, Int32(2), boxes[2])
    @test count_items(tree) == 1
    @test !is_member(tree, Int32(2), boxes[2])
    
    # Nearest neighbor
    # boxes[2] is (20,20,20,30,30,30)
    # boxes[3] is (10,10,10,25,25,25)
    # query at (35, 35, 35)
    # distance to boxes[2]: sqrt((35-30)^2 + (35-30)^2 + (35-30)^2) = sqrt(25*3) = sqrt(75)
    # distance to boxes[3]: sqrt((35-25)^2 + (35-25)^2 + (35-25)^2) = sqrt(100*3) = sqrt(300)
    
    near = nearest(tree, Int32(35), Int32(35), Int32(35), 1)
    @test length(near) == 1
    @test near[1].item == 3
    @test near[1].dist ≈ sqrt(300)

    # Badness
    badness(tree) # Should just print
    
    # Rebuild
    rebuild!(tree)
    @test count_items(tree) == 1
    @test is_member(tree, Int32(3), boxes[3])
    
    # Build tree
    new_boxes = [
        (Int32(0), Int32(0), Int32(0), Int32(5), Int32(5), Int32(5)),
        (Int32(10), Int32(10), Int32(10), Int32(15), Int32(15), Int32(15)),
        (Int32(20), Int32(20), Int32(20), Int32(25), Int32(25), Int32(25))
    ]
    new_items = Int32[1, 2, 3]
    tree2 = build_tree(new_items, new_boxes)
    @test count_items(tree2) == 3
    @test is_member(tree2, Int32(1), new_boxes[1])
    @test is_member(tree2, Int32(2), new_boxes[2])
    @test is_member(tree2, Int32(3), new_boxes[3])
end

@testset "KDTree3D Really Delete & Badness & Nearest" begin
    import KDTree3D: really_delete!
    using Random
    tree = Tree{Int32, Int32}()
    Random.seed!(42)
    
    # Generate 10,000 random boxes
    boxes = Box{Int32}[]
    for i in 1:10000
        x1 = rand(Int32(-100000):Int32(100000))
        y1 = rand(Int32(-100000):Int32(100000))
        z1 = rand(Int32(-100000):Int32(100000))
        x2 = x1 + rand(Int32(1):Int32(1000))
        y2 = y1 + rand(Int32(1):Int32(1000))
        z2 = z1 + rand(Int32(1):Int32(1000))
        b = (x1, y1, z1, x2, y2, z2)
        push!(boxes, b)
        insert!(tree, Int32(i), b)
    end
    
    @test count_items(tree) == 10000
    
    # 1. Test badness
    badness(tree) 
    
    # 2. Test nearest search
    for m in [1, 2, 4, 8, 16]
        for q in 1:50
            qx = rand(Int32(-100000):Int32(100000))
            qy = rand(Int32(-100000):Int32(100000))
            qz = rand(Int32(-100000):Int32(100000))
            
            list = nearest(tree, qx, qy, qz, m)
            @test length(list) == m
            
            for idx in 2:m
                @test list[idx].dist >= list[idx-1].dist - 1e-9
            end
            
            # Brute-force verification
            brute_dists = Float64[]
            for box in boxes
                dx = 0.0; dy = 0.0; dz = 0.0
                if qx > box[4] dx = Float64(qx - box[4]) elseif qx < box[1] dx = Float64(box[1] - qx) end
                if qy > box[5] dy = Float64(qy - box[5]) elseif qy < box[2] dy = Float64(box[2] - qy) end
                if qz > box[6] dz = Float64(qz - box[6]) elseif qz < box[3] dz = Float64(box[3] - qz) end
                push!(brute_dists, sqrt(dx*dx + dy*dy + dz*dz))
            end
            sort!(brute_dists)
            @test list[m].dist <= brute_dists[m] + 1e-6
        end
    end
    
    # 3. Test really_delete!
    for i in 10000:-1:1
        status, _, _ = really_delete!(tree, Int32(i), boxes[i])
        @test status == 1
    end
    
    @test count_items(tree) == 0
end
