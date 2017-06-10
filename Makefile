executables = bin/ptgz
objects = bin/ptgz-mpi.o

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

ptgz: src/ptgz-mpi.cpp $(objects)
	$(CC) $(CFLAGS) -o $(executables) $(objects)

bin/%.o: src/%.cpp | bin
	$(CC) $(CFLAGS) -c -o $@ $<

bin:
	mkdir -p %@

set-permissions:
	chmod -R 751 bin/

install: set-permissions
	cp $(executables) /usr/bin/ptgz
