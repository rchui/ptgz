all: clean gcc

clean:
	rm -rf bin/

gcc:
	mkdir bin/
	g++ -std=c++11 -fopenmp -O3 -o bin/ptgz src/ptgz.cpp
	chmod -R 751 bin/

icc: clean
	mkdir bin/
	icc -std=c++11 -openmp -O3 -o bin/ptgz src/ptgz.cpp
	chmod -R 751 bin/

mpi: clean
	mkdir bin/
	mpic++ -std=c++11 -openmp -O3 -o bin/ptgz src/ptgz-mpi.cpp
	chmod -R 751 bin/

icc-mpi: clean
	mkdir bin/
	mpic++ -std=c++11 -openmp -O3 -o bin/ptgz src/ptgz-mpi.cpp
	chmod -R 751 bin/

install:
	cp bin/ptgz /usr/bin/ptgz
