using Test
using KDTree
import KDTree: serialize, insert!, search, nearest, count_items, is_member, hard_delete!, really_delete!, badness
using Random

for CType in (Int32, Int64, Int128)
    @testset "KDTree Tests ($CType)" begin
        @testset "KDTree Basic & Hard Delete" begin
            tree = Tree{String, CType}()
            box1 = (CType(0), CType(0), CType(10), CType(10))
            box2 = (CType(20), CType(20), CType(30), CType(30))
            box3 = (CType(5), CType(5), CType(15), CType(15))

            insert!(tree, "item1", box1)
            insert!(tree, "item2", box2)
            insert!(tree, "item3", box3)

            @test count_items(tree) == 3
            @test is_member(tree, "item2", box2)

            found = search(tree, (CType(0), CType(0), CType(15), CType(15)))
            @test length(found) == 2
            items = [f[1] for f in found]
            @test "item1" in items
            @test "item3" in items

            @test hard_delete!(tree, "item1", box1) == true
            @test count_items(tree) == 2
            @test !is_member(tree, "item1", box1)
            @test is_member(tree, "item3", box3)
        end

        @testset "Stress Test ($CType)" begin
            tree = Tree{String, CType}()
            Random.seed!(42)
            
            boxes_to_delete = Tuple{String, Box{CType}}[]
            
            num_insert = 100_000 # Reduced from 1,000,000 so tests run fast across 3 types
            
            for i in 1:num_insert
                x1 = rand(CType(0):CType(99999))
                y1 = rand(CType(0):CType(99999))
                x2 = x1 + rand(CType(1):CType(100))
                y2 = y1 + rand(CType(1):CType(100))
                b = (x1, y1, x2, y2)
                
                name = "box$i"
                if i <= 1000
                    push!(boxes_to_delete, (name, b))
                end
                insert!(tree, name, b)
            end
            
            @test count_items(tree) == num_insert
            
            found = search(tree, (CType(0), CType(0), CType(50000), CType(50000)))
            @test length(found) > 1000 
            
            for (name, b) in boxes_to_delete
                success = hard_delete!(tree, name, b)
                @test success
            end
            
            @test count_items(tree) == num_insert - 1000
        end

        @testset "KDTree Really Delete & Badness & Nearest" begin
            tree = Tree{Int, CType}()
            Random.seed!(42)
            
            boxes = Box{CType}[]
            for i in 1:10000
                x1 = rand(CType(-100000):CType(100000))
                y1 = rand(CType(-100000):CType(100000))
                x2 = x1 + rand(CType(1):CType(1000))
                y2 = y1 + rand(CType(1):CType(1000))
                b = (x1, y1, x2, y2)
                push!(boxes, b)
                insert!(tree, i, b)
            end
            
            @test count_items(tree) == 10000
            
            badness(tree)
            
            for m in [1, 2, 4, 8, 16]
                for q in 1:50
                    qx = rand(CType(-100000):CType(100000))
                    qy = rand(CType(-100000):CType(100000))
                    
                    list = nearest(tree, qx, qy, m)
                    @test length(list) == m
                    
                    for idx in 2:m
                        @test list[idx].dist >= list[idx-1].dist - 1e-9
                    end
                    
                    brute_dists = Float64[]
                    for box in boxes
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
                    
                    @test list[m].dist <= brute_dists[m] + 1e-6
                end
            end
            
            qx = (boxes[1][LEFT] + boxes[1][RIGHT]) ÷ CType(2)
            qy = (boxes[1][BOTTOM] + boxes[1][TOP]) ÷ CType(2)
            list = nearest(tree, qx, qy, 1)
            @test list[1].dist <= 1e-9
            
            for i in 10000:-1:1
                status, _, _ = really_delete!(tree, i, boxes[i])
                @test status == 1
            end
            
            @test count_items(tree) == 0
            
            @test length(search(tree, (CType(-100001), CType(-100001), CType(100001), CType(100001)))) == 0
        
        @testset "Serialize ($CType)" begin
            tree = Tree{String, CType}()
            insert!(tree, "item1", (CType(10), CType(10), CType(10), CType(10)))
            insert!(tree, "item2", (CType(20), CType(20), CType(20), CType(20)))
            insert!(tree, "item3", (CType(5), CType(5), CType(5), CType(5)))

            # Test get_bounds
            @test get_bounds(tree) == (CType(5), CType(5), CType(20), CType(20))

            filename = "test_serialize_$(CType).kdtree"
            serialize(tree, filename, x -> parse(UInt64, replace(x, "item" => "")))
            
            node_size = 8 + 4*sizeof(CType) + 3*sizeof(CType) + 16
            @test filesize(filename) == 3 * node_size

            # Test get_serialized_bounds
            @test get_serialized_bounds(filename, CType, 2) == (CType(5), CType(5), CType(20), CType(20))
            
            rm(filename)
        end
end
    end
end