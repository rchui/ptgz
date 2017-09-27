# ptgz - Parallel Tar Gzip
The tar command line utility was first introduced in 1979 as a method of collecting many files into one archive file for distrbution and backup purposes. With the penetration of big data into every facet of the computing space datasets have grown exponentially in size. The single-threaded tar implementation is too slow to effectively archive terrabyte sized directories with millions of files. We introduce ptgz, a custom multi-threaded C++ file archiving utility to quickly compress, bundle files into an archive file, developed at the National Center for Supercomputing Applications.

## Requirements
    - C compiler with C++11 support.
    - OpenMP
    - MPI

## Installation
### GNU C Compiler
    make
    make install

Other compilers and flags can be used if desired. Simply set CC and CFLAGS when calling make.

## Usage
If you are compressing, your current working directory should be the parent directory of all directories you want to archive. If you are extracting, your current working directory should be the same as your archive.

ptgz will not preserve symlinks in the ptgz.tar archive. Instead, all symlinks will be replaced by copies of what is being symlinked to. Archives for directories with a lot of symlinks can turn out to be a lot bigger than expected.

### Command Syntax:
    ptgz [-c | -d </path/to/directory> | -k | -v | -x | -W] <archive>

### Modes:

    -c    Compression           Will perform file compression. The current directory and all of it's
                                children will be archived and added to a single tarball. <archive> will be 
                                prefix of the ptgz archive created.

    -d    Remote Directory      ptgz will compress and bundle a specified directory from a provided path.

    -k    Keep Archive          Does not delete the ptgz archive it has been passed to extract. This option 
                                must be used with "-x".
                                
    -l    Set Level             Instruct ptgz to use a specific compression level. Value must be from 1 to 9
                                1 is low compression, fast speed and 9 is high compression, low speed.

    -v    Enable Verbose        Will print the archive and removal commands as they are called to STDOUT.

    -x    Extraction            Signals for file extraction from an archive. The passed ptgz archive will be
                                unpacked and split into its component files. <archive> should be the name of
                                the archive to extract.

    -W    Verify Archive        Attempts to verify the archive after writing it.

## How it Works
### Compression
1) Single node, single threaded recursive traversal from the parent directory to build a record of all files.
2) The list of files is shuffled into random indexes in order to balance each compressed archive.
3) Single node, multi-threaded write to \*.ptgz.tmp files, which lists the files to be included in each \*.ptgz.tar.gz archive.
4) Multi-node, multi-threaded use of tar with level 1 (40% of original file size) gzip compression into \*.ptgz.tar.gz archives. 
5) Multi-node, maximum multi-rank per node use of mpitar to package all \*.ptgz.tar.gz archives into a single \*.ptgz.tar.

The compression process also includes in the \*.ptgz.tar archive:
  1) \*.sh: A tar-compatible single-threaded unpacking shell script if ptgz is not available.
  2) \*.idx: An index file of files contained within the \*.ptgz.tar archive. Each file is indexed by its \*.ptgz.tar.gz archive location.
  3) \*.ptgz.idx: An index of all \*.ptgz.tar.gz archives included that is used for \*.ptgz.tar archive extraction.
  4) \*.ptgz.tar.idx: An index file from mpitar which lists all of the \*.ptgz.tar.gz archives included in the \*.ptgz.tar archive and their starting byte location.

### Extraction
1) Single node, single threaded extraction \*.ptgz.idx file from \*.ptgz.tar archive.
2) Multi-node, multi-threaded extraction of \*.ptgz.tar.gz archives from \*.ptgz.tar archive using information from \*.ptgz.idx file.
3) Multi-node, multi-threaded extraction of all files in all \*.ptgz.tar.gz archives.

### TODO
1. Combine Makefiles
2. Convert mpitar.cc (remove INIT and change main to function)
3. Finish mpitar.hh