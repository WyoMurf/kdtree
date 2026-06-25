.PHONY: all test clean

all: test

test:
	julia --project=. -e 'using Pkg; Pkg.test()'

clean:
	rm -rf Manifest.toml
