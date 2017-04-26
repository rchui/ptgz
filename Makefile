all: clean gcc

clean:
	rm -rf bin/

icc:
	mkdir bin/
	icc -std=c++11 -openmp -O3 -o bin/ptgz src/ptgz.cpp
	chmod -R 751 bin/

gcc:
	mkdir bin/
	g++ -std=c++11 -fopenmp -O3 -o bin/ptgz src/ptgz.cpp
	chmod -R 751 bin/
