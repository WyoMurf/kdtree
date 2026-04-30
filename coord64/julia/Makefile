.PHONY: all test clean

all: test

test:
	julia kd_test.jl

clean:
	@echo "Cleaning Julia temporary files..."
	rm -f *.cov
	rm -f *~
