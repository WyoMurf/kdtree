using KDTree3D
using Test
import KDTree3D: serialize, insert!, search, nearest, count_items, is_member, hard_delete!, delete!, really_delete!, badness
using Random

for CType in (Int32, Int64, Int128, Float64)
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
                x1 = rand(CType(-20000):CType(20000))
                y1 = rand(CType(-20000):CType(20000))
                z1 = rand(CType(-20000):CType(20000))
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
                    qx = rand(CType(-20000):CType(20000))
                    qy = rand(CType(-20000):CType(20000))
                    qz = rand(CType(-20000):CType(20000))
                    
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
        
        @testset "Serialize ($CType)" begin
            tree = Tree{String, CType}()
            insert!(tree, "item1", (CType(10), CType(10), CType(10), CType(10), CType(10), CType(10)))
            insert!(tree, "item2", (CType(20), CType(20), CType(20), CType(20), CType(20), CType(20)))
            insert!(tree, "item3", (CType(5), CType(5), CType(5), CType(5), CType(5), CType(5)))

            # Test get_bounds
            @test get_bounds(tree) == (CType(5), CType(5), CType(5), CType(20), CType(20), CType(20))

            filename = "test_serialize_$(CType).kdtree"
            serialize(tree, filename, x -> parse(UInt64, replace(x, "item" => "")))
            
            node_size = 8 + 6*sizeof(CType) + 3*sizeof(CType) + 16
            @test filesize(filename) == 3 * node_size

            # Test get_serialized_bounds
            @test get_serialized_bounds(filename, CType, 3) == (CType(5), CType(5), CType(5), CType(20), CType(20), CType(20))

            rm(filename)
        end

        @testset "Tie-break regression ($CType)" begin
            # Regression coverage for a bug where hard_delete!'s promote-and-cascade
            # step could swap a different item into a node's tree position, changing
            # the "other axis" values used to break an exact coordinate tie --
            # silently misrouting later searches for an unrelated, never-deleted item.
            # Interleaving inserts and hard-deletes at this seed reliably reproduced
            # the bug before find_recursive/hard_delete_recursive! were fixed to check
            # (read-only) which side actually holds an item on a tie, instead of
            # guessing from the current node's other axes.
            state = Ref(UInt32(42))
            lcg_next() = (state[] = state[] * UInt32(1664525) + UInt32(1013904223); Int32(state[] >> 16))
            function lcg_range(maxv::Int32)
                v = lcg_next() % maxv
                v < 0 ? v + maxv : v
            end
            rand_box() = begin
                left = lcg_range(Int32(4001)) - Int32(2000)
                bottom = lcg_range(Int32(4001)) - Int32(2000)
                floor = lcg_range(Int32(4001)) - Int32(2000)
                (CType(left), CType(bottom), CType(floor),
                 CType(left + lcg_range(Int32(50))), CType(bottom + lcg_range(Int32(50))), CType(floor + lcg_range(Int32(50))))
            end

            n = 12000
            tree = Tree{Int, CType}()
            boxes = Vector{Box{CType}}(undef, n)
            deleted = falses(n)

            for i in 1:n
                b = rand_box()
                boxes[i] = b
                insert!(tree, i, b)

                if i % 3 == 0 && i > 1
                    victim = Int(lcg_range(Int32(i - 1))) + 1
                    if !deleted[victim] && hard_delete!(tree, victim, boxes[victim])
                        deleted[victim] = true
                    end
                end
            end

            all_found = true
            for j in 1:n
                if !deleted[j] && !is_member(tree, j, boxes[j])
                    all_found = false
                    break
                end
            end
            @test all_found
        end
end
    end
end

@testset "Geo/Angle Utilities" begin
    @testset "DMS round-trip ($CType)" for CType in (Int32, Int64, Int128, Float64)
        # Negative-near-zero case: sign=-1, deg=0, min=15, sec=0.0 must decode to exactly -0.25
        @test dms_to_degrees(-1, CType(0), CType(15), 0.0) == -0.25

        test_values = [0.0, -0.25, 45.5, -45.5, 90.0, -90.0, 179.999, -179.999, 12.3456789, -0.0001]
        for x in test_values
            sign, deg, min, sec = degrees_to_dms(CType, x)
            @test isapprox(dms_to_degrees(sign, deg, min, sec), x; atol=1e-9)
        end
    end

    @testset "Haversine quarter great-circle" begin
        for R in (1.0, EARTH_RADIUS_KM, 6378137.0)
            d = haversine_distance(0.0, 0.0, 90.0, 0.0, R)
            @test isapprox(d, R * pi / 2.0; atol=1e-9)
        end
    end

    @testset "Vincenty equatorial exact circle" begin
        delta = 10.0
        d = vincenty_distance(0.0, 0.0, 0.0, delta, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING)
        expected = EARTH_SEMI_MAJOR_AXIS_M * deg2rad(delta)
        @test isapprox(d, expected; atol=1e-6)
    end

    @testset "Vincenty near-antipodal points don't hang or error" begin
        d = vincenty_distance(0.0, 0.0, 0.001, 179.999, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING)
        @test isfinite(d)
    end

    # Expected values cross-checked against C/healpix_calc.c (via the shared
    # geo_utils.c implementation it now wraps), which this port mirrors exactly.
    @testset "HEALPix nested index matches C reference" begin
        @test healpix_nested_index(217.4290, -62.6795, 12) == 134053741  # polar cap
        @test healpix_nested_index(-109.05653, 44.52634, 3) == 330       # polar cap, negative lon
        @test healpix_nested_index(45.0, 10.0, 3) == 282                 # equatorial belt
        @test healpix_nested_index(0.0, 0.0, 3) == 256                   # equatorial belt, origin
        @test healpix_nested_index(200.0, -20.0, 5) == 4257              # equatorial belt, higher level
    end

    @testset "HEALPix nested index longitude normalization" begin
        @test healpix_nested_index(-109.05653, 44.52634, 3) == healpix_nested_index(250.94347, 44.52634, 3)
    end
end
