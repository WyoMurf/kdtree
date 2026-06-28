.PHONY: all test clean

all: 
	$(MAKE) -C C
	cd 2d/go && go build ./...
	cd 2d/rust && cargo build
	$(MAKE) -C 2d/julia
	cd 3d/go && go build ./...
	cd 3d/rust && cargo build
	$(MAKE) -C 3d/julia

test: 
	$(MAKE) -C C test
	cd 2d/go && go test -v ./...
	cd 2d/rust && cargo test
	$(MAKE) -C 2d/julia test
	cd 3d/go && go test -v ./...
	cd 3d/rust && cargo test
	$(MAKE) -C 3d/julia test

clean:
	$(MAKE) -C C clean
	cd 2d/go && go clean
	cd 2d/rust && cargo clean
	$(MAKE) -C 2d/julia clean
	cd 3d/go && go clean
	cd 3d/rust && cargo clean
	$(MAKE) -C 3d/julia clean
