# ptgz - Parallel Tar Gzip
The tar command line utility was first introduced in 1979 as a method of collecting many files into one archive file for distrbution and backup purposes. With the penetration of big data into every facet of the computing space datasets have grown exponentially in size. The single-threaded tar implementation is too slow to effectively archive terrabyte sized directories with millions of files. We introduce ptgz, a custom multi-threaded C++ file archiving utility to quickly compress, bundle files into an archive file, developed at the National Center for Supercomputing Applications.

## Requirements
Need at least either GNU C Compiler (4.9+) with OpenMP or Intel C Compiler with OpenMP

Compiler must have C++11 support.

Requires MPI if desired but can be compiled without.

## Installation - OpenMP
Specific compilation commands can be found in the Makefile.

### GNU C Compiler
    make
    make install

### Intel C Compiler
    make icc
    make install

## Installation - OpenMP + MPI
### GNU C Compiler
    make mpi
    make install

### Intel C Compiler
    make icc-mpi
    make install

## Usage
If you are compressing, your current working directory should be the parent directory of all directories you want to archive. If you are extracting, your current working directory should be the same as your archive.

ptgz will not preserve symlinks or store empty directories in the ptgz.tar archive. Instead, all symlinked files/directory will be replaced by copies of the symlinked file/directory. Archives for directories with a lot of symlinks can turn out to be a lot bigger than expected.

### Command Syntax:
    ptgz [-c | -k | -v | -x | -W] <archive>

### Modes:

    -c    Compression           Will perform file compression. The current directory and all of it's
                                children will be archived and added to a single tarball. <archive> will be 
                                prefix of the ptgz archive created.

    -k    Keep Archive          Does not delete the ptgz archive it has been passed to extract. This option 
                                must be used with "-x".
                                
    -l    Set Level             Instruct ptgz to use a specific compression level. Value must be from 1 to 9
                                1 is low compression, fast speed and 9 is high compression, low speed.

    -v    Enable Verbose        Will print the archive and removal commands as they are called to STDOUT.

    -x    Extraction            Signals for file extraction from an archive. The passed ptgz archive will be
                                unpacked and split into its component files. <archive> should be the name of
                                the archive to extract.

    -W    Verify Archive        Attempts to verify the archive after writing it.
