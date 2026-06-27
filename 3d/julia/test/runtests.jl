using KDTree3D
using Test
import KDTree3D: insert!, search, nearest, count_items, is_member, hard_delete!, delete!, really_delete!, badness
using Random

for CType in (Int32, Int64, Int128)
    @testset "KDTree3D Tests ($CType)" begin
        @testset "KDTree3D basic tests" begin
            tree = Tree{Int32, CType}()
            
            boxes = [
                (CType(0), CType(0), CType(0), CType(10), CType(10), CType(10)),
                (CType(20), CType(20), CType(20), CType(30), CType(30), CType(30)),
                (CType(10), CType(10), CType(10), CType(25), CType(25), CType(25))
            ]
            
            for (i, box) in enumerate(boxes)
                insert!(tree, Int32(i), box)
            end
            
            @test count_items(tree) == 3
            
            @test is_member(tree, Int32(1), boxes[1])
            @test is_member(tree, Int32(2), boxes[2])
            @test is_member(tree, Int32(3), boxes[3])
            @test !is_member(tree, Int32(4), (CType(0),CType(0),CType(0),CType(0),CType(0),CType(0)))
            
            results = search(tree, (CType(5), CType(5), CType(5), CType(15), CType(15), CType(15)))
            @test length(results) == 2
            items = [r[1] for r in results]
            @test 1 in items
            @test 3 in items
            @test !(2 in items)
            
            @test hard_delete!(tree, Int32(1), boxes[1])
            @test count_items(tree) == 2
            @test !is_member(tree, Int32(1), boxes[1])
            
            @test delete!(tree, Int32(2), boxes[2])
            @test count_items(tree) == 1
            @test !is_member(tree, Int32(2), boxes[2])
            
            near = nearest(tree, CType(35), CType(35), CType(35), 1)
            @test length(near) == 1
            @test near[1].item == 3
            @test near[1].dist ≈ sqrt(300)
        end

        @testset "KDTree3D Really Delete & Badness & Nearest" begin
            tree = Tree{Int32, CType}()
            Random.seed!(42)
            
            boxes = Box{CType}[]
            for i in 1:10000
                x1 = rand(CType(-100000):CType(100000))
                y1 = rand(CType(-100000):CType(100000))
                z1 = rand(CType(-100000):CType(100000))
                x2 = x1 + rand(CType(1):CType(1000))
                y2 = y1 + rand(CType(1):CType(1000))
                z2 = z1 + rand(CType(1):CType(1000))
                b = (x1, y1, z1, x2, y2, z2)
                push!(boxes, b)
                insert!(tree, Int32(i), b)
            end
            
            @test count_items(tree) == 10000
            
            badness(tree) 
            
            for m in [1, 2, 4, 8, 16]
                for q in 1:50
                    qx = rand(CType(-100000):CType(100000))
                    qy = rand(CType(-100000):CType(100000))
                    qz = rand(CType(-100000):CType(100000))
                    
                    list = nearest(tree, qx, qy, qz, m)
                    @test length(list) == m
                    
                    for idx in 2:m
                        @test list[idx].dist >= list[idx-1].dist - 1e-9
                    end
                    
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
            
            for i in 10000:-1:1
                status, _, _ = really_delete!(tree, Int32(i), boxes[i])
                @test status == 1
            end
            
            @test count_items(tree) == 0
        end
    end
end