executables = bin/ptgz

### Choose an appropriate compiler
### Choose appropriate compiler flags

## MPI C++ Compiler and Flags
CC = mpic++

## GCC
CFLAGS := -std=c++11 -fopenmp -O3

## Intel
# CFLAGS := -std=c++11 -openmp -O3

## Cray C++ Compiler and Flags
# CC = CC
# CFLAGS := -std=c++11 -fopenmp -O3


all: ptgz

clean:
	rm -rf bin/
	rm -rf src/*.o

make-directory:
	mkdir -p bin/

ptgz: src/ptgz-mpi.cpp src/ptgz-mpi.o make-directory
	$(CC) $(CFLAGS) -o $(executables) src/ptgz-mpi.o

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

set-permissions:
	chmod -R 751 bin/

install: set-permissions
	cp $(executables) /usr/bin/ptgz
