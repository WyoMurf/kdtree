
all: 
	cd C; make; cd ../
	cd 2d/go; go build; cd ../..
	cd 2d/rust; cargo test; cd ../..
	cd 2d/julia; make; cd ../..
	cd 3d/go; go build; cd ../..
	cd 3d/rust; cargo test; cd ../..
	cd 3d/julia; make; cd ../..
	
