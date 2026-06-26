using KDTree3D
using Random
import KDTree3D: insert!, search, hard_delete!

function benchmark()
    tree = Tree{Int32, Int32}()
    Random.seed!(42)
    
    n_boxes = 1_000_000
    boxes = Vector{Box{Int32}}(undef, n_boxes)
    
    println("Inserting $n_boxes boxes...")
    start = time()
    for i in 1:n_boxes
        x1 = rand(Int32(0):Int32(1_000_000_000))
        y1 = rand(Int32(0):Int32(1_000_000_000))
        z1 = rand(Int32(0):Int32(1_000_000_000))
        x2 = x1 + rand(Int32(1):Int32(100))
        y2 = y1 + rand(Int32(1):Int32(100))
        z2 = z1 + rand(Int32(1):Int32(100))
        
        boxes[i] = (x1, y1, z1, x2, y2, z2)
        insert!(tree, Int32(i), boxes[i])
    end
    println("Insertion took $(time() - start)s")
    
    search_area = (Int32(0), Int32(0), Int32(0), Int32(500_000_000), Int32(500_000_000), Int32(500_000_000))
    println("Searching in area $search_area...")
    start = time()
    results = search(tree, search_area)
    println("Found $(length(results)) boxes in search area. Search took $(time() - start)s")
    
    println("Deleting 1000 items...")
    start = time()
    for i in 1:1000
        hard_delete!(tree, Int32(i), boxes[i])
    end
    println("Deletion took $(time() - start)s")
    println("Done.")
end

if abspath(PROGRAM_FILE) == @__FILE__
    benchmark()
end