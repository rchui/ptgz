executables = bin/ptgz

### Choose an appropriate compiler
### Choose appropriate compiler flags

## GNU C++ Compiler and Flags
CC = g++
CFLAGS := -std=c++11 -fopenmp -O3

## Intel C++ Compiler and Flags
# CC = icc
# CFLAGS := -std=c++11 -openmp -O3

## MPI C++ Compiler and Flags
## Use whichevcr flags are appropriate
# CC = mpic++
# CFLAGS := -std=c++11 -fopenmp -O3
# CFLAGS := -std=c++11 -openmp -O3

## Cray C++ Compiler and Flags
# CC = CC
# CFLAGS := -std=c++11 -fopenmp -O3


all: clean ptgz

clean:
	rm -rf bin/
	rm -rf src/*.o

make-directory:
	mkdir -p bin/

ptgz: src/ptgz.cpp src/ptgz.o make-directory
	$(CC) $(CFLAGS) -o $(executables) src/ptgz.o

ptgz-mpi: src/ptgz-mpi.cpp src/ptgz-mpi.o make-directory
	$(CC) $(CFLAGS) -o $(executables) src/ptgz-mpi.o

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

set-permissions:
	chmod -R 751 bin/

install: set-permissions
	cp $(objects) /usr/$(objects)
