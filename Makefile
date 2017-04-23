all: clean build

clean:
	rm -rf bin/

build:
	mkdir bin/
	g++ -std=c++11 -fopenmp -o bin/ptxz src/ptxz.cpp
	chmod -R 751 bin/
