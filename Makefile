executables = bin/ptgz

### Choose an appropriate compiler
## GNU C++ Compiler
CC = g++
## Intel C++ Compiler
# CC = icc
## MPI C++ Compiler
# CC = mpic++
## Cray C++ Compiler
# CC = CC

### Choose appropriate compiler flags
## GNU/Cray C++ Compiler Flags
CFLAGS := -std=c++11 -fopenmp -O3
## Intel C++ Compiler Flags
# CFLAGS := -std=c++11 -openmp -O3 -c

all: ptgz

clean:
	rm -rf bin/
	rm -rf src/*.o

make-directory:
	mkdir -p bin/

ptgz: make-directory src/ptgz.o
	$(CC) $(CFLAGS) -o $(executables) src/ptgz.o

ptgz-mpi: make-directory src/ptgz.o
	$(CC) $(CFLAGS) -o $(executables) src/ptgz.o

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

set-permissions:
	chmod -R 751 bin/

install: set-permissions
	cp $(objects) /usr/$(objects)
