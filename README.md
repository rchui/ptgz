# ptgz

ptgz - Parallel Tar GZ by Ryan Chui (2017)

    ptgz is a custom multi-threaded C++ file archiving utility to quickly bundle millions of files in 
    terrabyte sized directories into a single file. ptgz was developed at the National Center for 
    Supercomputing Applications.

    Usage:
    If you are compressing, your current working directory should be parent directory of all directories you
    want to archive. If you are extracting, your current working directory should be the same as your archive.

    ptgz [-c|-k|-v|-x] <archive>

    Modes:
    -c    Compression           ptgz will perform file compression. The current directory and all of it's
                                children will be archived and added to a single tarball. <archive> will be 
                                prefix of the ptgz archive created.

    -k    Keep Archive          ptgz will not delete the ptgz archive it has been passed to extract. This 
                                option must be used with "-x".

    -v    Enable Verbose        ptgz will print the commands as they are called to STDOUT

    -x    Extraction            ptgz will perform file extraction from an archive. The passed ptgz archive
                                will be unpacked and split into its component files. <archive> should be the
                                the name of the archive to extract.
