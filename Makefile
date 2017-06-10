executables = bin/ptgz
objects = obj/ptgz-mpi.o
sources = src/ptgz-mpi.cpp

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
	rm -rf bin/ obj/

ptgz: $(sources) $(objects) mpitar | bin
	$(CC) $(CFLAGS) -o $(executables) $(objects)

mpitar:
	cd mpitar/ && make CC=$(CC) && cp mpitar ../bin

obj/%.o: src/%.cpp | obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj:
	mkdir -p $@

bin:
	mkdir -p $@

set-permissions:
	chmod -R 751 bin/

install: set-permissions
	cp $(executables) /bin/ptgz
