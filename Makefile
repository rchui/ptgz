all: clean build

clean:
	rm -rf bin/

build:
	mkdir bin/
	g++ -fopenmp -o bin/ptxz src/ptxz.cpp
	chmod -R 751 bin/
