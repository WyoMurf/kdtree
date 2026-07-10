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
