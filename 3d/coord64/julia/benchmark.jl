using KDTree3D
using Random
import KDTree3D: insert!, search, hard_delete!

function benchmark()
    tree = Tree{Int}()
    Random.seed!(42)
    
    n_boxes = 1_000_000
    boxes = Vector{Box}(undef, n_boxes)
    
    println("Inserting $n_boxes boxes...")
    start = time()
    for i in 1:n_boxes
        x1 = rand(0:9_999_999_999)
        y1 = rand(0:9_999_999_999)
        z1 = rand(0:9_999_999_999)
        x2 = x1 + rand(1:100)
        y2 = y1 + rand(1:100)
        z2 = z1 + rand(1:100)
        
        boxes[i] = (x1, y1, z1, x2, y2, z2)
        insert!(tree, i, boxes[i])
    end
    println("Insertion took $(time() - start)s")
    
    search_area = (0, 0, 0, 5_000_000_000, 5_000_000_000, 5_000_000_000)
    println("Searching in area $search_area...")
    start = time()
    results = search(tree, search_area)
    println("Found $(length(results)) boxes in search area. Search took $(time() - start)s")
    
    println("Deleting 1000 items...")
    start = time()
    for i in 1:1000
        hard_delete!(tree, i, boxes[i])
    end
    println("Deletion took $(time() - start)s")
    println("Done.")
end

if abspath(PROGRAM_FILE) == @__FILE__
    benchmark()
end
