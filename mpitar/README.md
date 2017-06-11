mpitar
======

What is mpitar?
---------------

mpitar is a proof of concept implementation of a MPI parallelized tar comamnd
that can currently produce tar files using multiple MPI processes
reading/writing files in parallel. It currently supports regular files,
symbolic links and directories.

Beyond the tarfile it also produces an index file that lists the tar file
content and the offset of each tar member in the tar file. This index is
included as the last file in the archive itself. A proof-of-concept chop.pl
code is included that can take (a subset of) the lines in the index file and
produce a tar file containing only those files.

Installation
------------

This has only been test on Linux systems so far where an MPI implemenation and
mpic++ or similar is required to compile. Please set CC in the make file to
your compiler or do:

```
make CC=mpiCC
```

Usage
-----
```
find . -type f -or -type d -or -type l >files.txt
mpirun -n 3 mpitar -c -f feather.tar -T files.txt
```
or
```
mpirun -n 3 mpitar -c -f feather.tar file1 file2 dir2 ...
```

Partial extraction works liks so (to extract e. g. every second file):
```
awk 'NR%2' <feather.tar.idx >every_second.idx
choptar.pl every_second.idx feather.tar | tar -t
```
and
```
extractindex.pl feather.tar >feather.tar.idx
```
extracts the index file from the end of the tar file.

TODO
----

1. ~~write index file listing file name and offset~~
1. ~~sort index file by file name for binary search~~ won't do since this makes chopping hard
1. add parallel file extractor code
1. ~~have mpitar take file and directory names on the command line~~
1. ~~recurse into directories given on the command line~~
1. make sure it works eg on OSX (low priority though)
1. ~~make error reporting work, do not use assert() for this~~
1. use `MPI_IO` (not sure what the benefit would be)
1. provide some scaling numbers
1. add option to split tar file into X GB smaller tar files
1. make BUFFER size, number of files per work package, work package size etc. runtime options
