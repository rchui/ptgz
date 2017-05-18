#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <queue>
#include <utility>
#include "omp.h"
#include "mpi.h"

int root = 0;
int globalRank, globalSize;

// Contains the various options the user can pass ptgz.
// Members: extract (bool) whether ptgz should be extracting.
//	    compress (bool) whether ptgz should be compressing.
//	    verbose (bool) whether ptgz should output commands.
//	    keep (bool) whether ptgz should keep the extracted arvhive.
//	    verify (bool) whether ptgz should verify the compressed archive.
//	    name (std::string) name of archive to make or extract.
struct Settings {
	Settings(): level(),
				levelSet(),
				extract(),
				compress(),
				verbose(),
				keep(),
				output(),
				verify(),
				name() {}
	int64_t level;
	bool levelSet;
	bool extract;
	bool compress;
	bool verbose;
	bool keep;
	bool output;
	bool verify;
	std::string name;
};

// Checks if the user asks for help.
// Provides usage information to the user.
// Parameters: argc (int) number of cli arguments.
// 			   argv (char *[]) user provided arguments.
void helpCheck(int argc, char *argv[]) {
	if (argc == 1) {
		std::cout << "ERROR: ptgz was passed no parameters. \"ptgz -h\" for help." << std::endl;
		exit(0);
	}

	if (argv[1] == std::string("-h") || argv[1] == std::string("--help")) {
		std::cout << "\nptgz - Parallel Tar GZ by Ryan Chui (2017)\n" << std::endl;
		std::cout << "    ptgz is a custom multi-threaded C++ file archiving utility to quickly bundle millions of files in \n";
		std::cout << "    terrabyte sized directories int64_to a single file. ptgz was developed at the National Center for \n";
		std::cout << "    Supercomputing Applications.\n" << std::endl;
		std::cout << "    Usage:\n";
		std::cout << "    If you are compressing, your current working directory should be parent directory of all directories you\n";
		std::cout << "    want to archive. If you are extracting, your current working directory should be the same as your archive.\n" << std::endl;
		std::cout << "    ptgz [-c|-k|-v|-x|-W] <archive>\n" << std::endl;
		std::cout << "    Modes:\n";
		std::cout << "    -c    Compression           Will perform file compression. The current directory and all of it's\n";
		std::cout << "                                children will be archived and added to a single tarball. <archive> will be \n";
		std::cout << "                                prefix of the ptgz archive created.\n" << std::endl;
		std::cout << "    -k    Keep Archive          Dooes not delete the ptgz archive it has been passed to extract. \"-x\" must\n";
		std::cout << "                                also be used to use this option.\n" << std::endl;
		std::cout << "    -v    Enable Verbose        Will print the commands as they are called to STDOUT\n" << std::endl;
		std::cout << "    -x    Extraction            Signals for file extraction from an archive. The passed ptgz archive will be\n";
		std::cout << "                                unpacked and split int64_to its component files. <archive> should be the name of\n";
		std::cout << "                                the archive to extract.\n" << std::endl;
		std::cout << "    -W    Verify Archive        Attempts to verify the archive after writing it.\n" << std::endl;
		exit(0);
	}
}

// Gets the parameters passed by the user and stores them.
// Parameters: argc (int) number of cli arguments.
// 			   argv (char *[]) user provided arguments.
// 			   instance (Settings *) user argument storage.
void getSettings(int argc, char *argv[], Settings *instance) {
	// Get all passed arguments
	std::queue<std::string> settings;
	for (int64_t i = 1; i < argc; ++i) {
		settings.push(argv[i]);
	}
	
	// Continue to check until there are no more passed arguments.
	while (!settings.empty()) {
		std::string arg = settings.front();

		if (arg == "-x") {
			if ((*instance).compress) {
				std::cout << "ERROR: ptgz cannot both compress and extract. \"ptgz -h\" for help." << std::endl;
				exit(0);
			}
			(*instance).extract = true;
		} else if (arg == "-c") {
			if ((*instance).extract) {
				std::cout << "ERROR: ptgz cannot both compress and extract. \"ptgz -h\" for help." << std::endl;
				exit(0);
			}
			(*instance).compress = true;
		} else if (arg == "-v"){
			(*instance).verbose = true;
		} else if (arg == "-o") {
			(*instance).output = true;
		} else if (arg == "-k") {
			(*instance).keep = true;
		} else if (arg == "-W") {
			(*instance).verify = true;
		} else if (arg == "-l") { 
			(*instance).levelSet = true;
			settings.pop();
			int64_t level = std::stoi(settings.front());
			if (level >= 1 && level <= 9) {
				(*instance).level = level;
			} else {
				std::cout << "ERROR: level must be set from 1 to 9." << std::endl;
				exit(0);
			}
		} else {
			if (settings.size() > 1) {
				std::cout << "ERROR: ptgz was called incorrectly. \"ptgz -h\" for help." << std::endl;
				exit(0);
			}
			(*instance).output = true;
			(*instance).name = arg;
		}

		settings.pop();
	}

	if (!(*instance).output) {
		std::cout << "ERROR: No output file name given. \"ptgz -h\" for help." << std::endl;
		exit(0);
	} else if ((*instance).keep && !(*instance).extract) {
		std::cout << "ERROR: Can't use keep option without extract. \"ptgz -h\" for help." << std::endl;
	}
}

// Gets the paths for all files in the space to store.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   cwd (const char *) current working directory.
// 			   rootPath (std::string) path from the root of the directory to be stored.
void getPaths(std::vector<std::string> *filePaths, const char *cwd, std::string rootPath) {
	DIR *dir;
	struct dirent *ent;

	// Check if cwd is a directory
	if ((dir = opendir(cwd)) != NULL) {
		// Get all file paths within directory.
		while ((ent = readdir (dir)) != NULL) {
			std::string fileBuff = std::string(ent -> d_name);
			if (fileBuff != "." && fileBuff != "..") {
				DIR *dir2;
				std::string filePath = std::string(cwd) + "/" + fileBuff;
				// Check if file path is a directory.
				if ((dir2 = opendir(filePath.c_str())) != NULL) {
					closedir(dir2);
					getPaths(filePaths, filePath.c_str(), rootPath + fileBuff + "/");
				} else {
					filePaths->push_back(rootPath + fileBuff);
				}
			}
		}
		closedir(dir);
	}
}

// Divides files int64_to blocks.
// Compresses each block int64_to a single file.
// Combines all compressed blocks int64_to a single file.
// Removes temporary blocks and header files.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   name (std::string) user given name for storage file.
// 			   verbose (bool) user option for verbose output.
//			   verify (bool) user option for tar archive verification.
void compression(std::vector<std::string> *filePaths, std::string name, bool verbose, bool verify, bool levelSet, int64_t level) {
	std::random_shuffle(filePaths->begin(), filePaths->end());

	uint64_t filePathSize = filePaths->size();
	MPI_Bcast(&filePathSize, 1, MPI_UINT64_T, root, MPI_COMM_WORLD);
	std::vector<std::string> *tarNames;

	int64_t numBlocks = omp_get_max_threads() * 10 * globalSize;
	uint64_t blockSize;
	int64_t *sendSizes = new int64_t[globalSize * 2];
	int64_t reserved = 0;
	int64_t tarBlock;
	int64_t *localSize = new int64_t[2];

	std::cout << "numBlocks = " + std::to_string(numBlocks) + "\n";

	// Get blockSize and set tarName vector size.
	if (filePathSize % numBlocks == 0) {
		blockSize = (filePathSize / numBlocks);
	} else {
		blockSize = (filePathSize / numBlocks) + 1;
	}

	std::cout << "blockSize = " + std::to_string(blockSize) + "\n";

	if (filePathSize % blockSize == 0) {
		tarNames = new std::vector<std::string>(filePathSize / blockSize);
	} else {
		tarNames = new std::vector<std::string>(filePathSize / blockSize + 1);
	}

	std::cout << "globalSize = " + std::to_string(globalSize) + "\n";

	if (globalRank == root) {
		// Write all files to text files.
		#pragma omp parallel for schedule(static)
		for (int64_t i = 0; i < numBlocks; ++i) {
			uint64_t start = blockSize * i;
			if (start < filePathSize) {
				std::ofstream tmp;
				tmp.open(std::to_string(i) + "." + name + ".ptgz.tmp", std::ios_base::app);
				for (uint64_t j = start; j < std::min(start + blockSize, filePathSize); ++j) {
					tmp << filePaths->at(j) + "\n";
				}
				tmp.close();
				tarNames->at(i) = std::to_string(i) + "." + name + ".tar.gz";
			}
		}
		filePaths->clear();
		delete(filePaths);

		// Get tar archive block size.
		if (tarNames->size() % globalSize == 0) {
			tarBlock = tarNames->size() / globalSize;
		} else {
			tarBlock = tarNames->size() / globalSize + 1;
		}

		// Set blocks for each rank.
		for (int64_t i = 0; i < globalSize * 2; i += 2) {
			sendSizes[i] = reserved;
			reserved += tarBlock;
			if (reserved <= tarNames->size()) {
				sendSizes[i + 1] = tarBlock;
			} else {
				sendSizes[i + 1] = tarBlock - (reserved - tarNames->size());
			}
		}
	}

	// Send blocks to all ranks then build tar archives.
	MPI_Scatter(sendSizes, 2, MPI_INT64_T, localSize, 2, MPI_INT64_T, root, MPI_COMM_WORLD);

	// Build tar archives for each block
	#pragma omp parallel for schedule(dynamic)
	for (int64_t i = localSize[0]; i < localSize[0] + localSize[1]; ++i) {
		std::string gzCommand;
		if (!levelSet) {
			gzCommand = std::to_string(globalRank) + " tar -c -T " + std::to_string(i) + "." + name + ".ptgz.tmp | gzip -1 > " + std::to_string(i) + "." + name + ".tar.gz";
		} else {
			gzCommand = std::to_string(globalRank) + " tar -c -T " + std::to_string(i) + "." + name + ".ptgz.tmp | gzip -" + std::to_string(level) + " > " + std::to_string(i) + "." + name + ".tar.gz";
		}
		if (verbose) {
			std::cout << gzCommand + "\n";
		}
		system(gzCommand.c_str());
	}

	if (globalRank == root) {
		// Combines gzipped blocks together int64_to a single tarball.
		// Write tarball names int64_to an idx file for extraction.
		std::ofstream idx, tmp;
		idx.open(name + ".ptgz.idx", std::ios_base::app);
		std::string tarCommand;
		if (!verify) {
			tarCommand = "tar -c -T " + name + ".ptgz.idx -f " + name + ".ptgz.tar";	
		} else {
			tarCommand = "tar -c -W -T " + name + ".ptgz.idx -f " + name + ".ptgz.tar";
		}
		for (uint64_t i = 0; i < tarNames->size(); ++i) {
			idx << tarNames->at(i) + "\n";
		}
		idx << name + ".ptgz.idx\n";
		idx.close();
		
		if (verbose) {
			std::cout << tarCommand + "\n";
		}
	
		system(tarCommand.c_str());
	}

	MPI_Barrier(MPI_COMM_WORLD);

	// Removes all temporary blocks.
	#pragma omp parallel for schedule(static)
	for (uint64_t i = localSize[0]; i < localSize[0] + localSize[1]; ++i) {
		std::string rmCommand = std::to_string(i) + "." + name + ".tar.gz";
		if (verbose) {
			std::cout << "remove(" + rmCommand + ")\n";
		}
		if (remove(rmCommand.c_str())) {
			std::cout << "ERROR: " + rmCommand + " could not be removed.\n";
		}

		rmCommand = std::to_string(i) + "." + name + ".ptgz.tmp";
		if (verbose) {
			std::cout << "remove(" + rmCommand + ")\n";
		}
		if (remove(rmCommand.c_str())) {
			std::cout << "ERROR: " + rmCommand + " could not be removed.\n";
		}
	}

	delete(sendSizes);
	delete(localSize);

	if (globalRank == root) {
		// Removes idx file.
		std::string rmCommand;
		if (verbose) {
			std::cout << "remove(" + name + ".ptgz.idx)\n";
		}
		rmCommand = name + ".ptgz.idx";
		if (remove(rmCommand.c_str())) {
			std::cout << "ERROR: " + rmCommand + " could not be removed.\n";
		}

		tarNames->clear();
		delete(tarNames);
	}
	MPI_Finalize();
}

// Gets and returns the size of a file
// Parameters: filename (std::string) name of the file whose size to find.
uint64_t GetFileSize(std::string filename)
	{
		struct stat stat_buf;
		uint64_t rc = stat(filename.c_str(), &stat_buf);
		return rc == 0 ? stat_buf.st_size : -1;
	}

// Unpacks the archive.
// Reads in all the files from the index file.
// Unpacks each file.
// Deletes all temporary file blocks and header files.
// Parameters: name (std::string) name of ptgz archive file.
// 			   verbose (bool) user option for verbose output.
// 			   keep (bool) user option for keeping ptgz archive.
void extraction(std::string name, bool verbose, bool keep) {
	// Get the name from the name of the 1st layer tarball
	for (int64_t i = 0; i < 9; ++i) {
		name.pop_back();
	}

	int64_t numArchives;

	if (globalRank == root) {
		// Unpack index from the 1st layer tar ball
		std::string exCommand = "tar xf " + name + ".ptgz.tar " + name + ".ptgz.idx";
		if (verbose) {
			std::cout << exCommand + "\n";
		}
		system(exCommand.c_str());

		// Get number of archives and delete index.
		std::ifstream idx;
		std::string line;
		numArchives = 0;
		idx.open(name + ".ptgz.idx", std::ios_base::in);
		while (std::getline(idx, line)) {
			++numArchives;
		}
		--numArchives;
		idx.close();
		std::string idxRmCommand = name + ".ptgz.idx";
		if (verbose) {
			std::cout << "remove(" + idxRmCommand + ")\n";
		}
		if (remove(idxRmCommand.c_str())) {
			std::cout << "ERROR: " + idxRmCommand + " could not be removed.\n";
		}
	}

	int64_t blockSize;
	int64_t *sendBlocks = new int64_t[globalSize * 2];
	int64_t *localBlock = new int64_t[2];

	if (globalRank == root) {
		// Define block size
		if (numArchives % globalSize == 0) {
			blockSize = numArchives / globalSize;
		} else {
			blockSize = numArchives / globalSize + 1;
		}

		// Define blocks for each node.
		int64_t reserved = 0;
		for (int64_t i = 0; i < globalSize * 2; i += 2) {
			sendBlocks[i] = reserved;
			reserved += blockSize;
			if (reserved <= numArchives) {
				sendBlocks[i + 1] = blockSize;
			} else {
				sendBlocks[i + 1] = blockSize - (reserved - numArchives);
			}
		}
	}

	// Send each node their block
	MPI_Scatter(sendBlocks, 2, MPI_INT64_T, localBlock, 2, MPI_INT64_T, root, MPI_COMM_WORLD);
	// printf("Process %d, %d, %d\n", globalRank, localBlock[0], localBlock[1]);

	// Extract compressed archives from ptgz.tar archive
	#pragma omp parallel for schedule(dynamic)
	for (uint64_t i = localBlock[0]; i < localBlock[0] + localBlock[1]; ++i) {
		std::string tarCommand = "tar xf " + name + ".ptgz.tar " + std::to_string(i) + "." + name + ".tar.gz";
		system(tarCommand.c_str());
	}

	// Fill weights vector and sort by file size descending
	std::vector<std::pair<uint64_t, std::string>> *weights = new std::vector<std::pair<uint64_t, std::string>>(localBlock[1]);
	#pragma omp parallel for schedule(static)
	for (uint64_t i = 0; i < localBlock[1]; ++i) {
		std::string archiveName = std::to_string(i + localBlock[0]) + "." + name + ".tar.gz";
		weights->at(i) = std::make_pair(GetFileSize(archiveName), archiveName);
	}
	std::sort(weights->rbegin(), weights->rend());

	// Unpack each tar.gz file.
	#pragma omp parallel for schedule(dynamic)
	for (uint64_t i = 0; i < weights->size(); ++i) {
		std::string gzCommand = "tar xzf " + weights->at(i).second;
		if (verbose) {
			std::cout << gzCommand + "\n";
		}
		system(gzCommand.c_str());
	}

	// Double check unpacking.
	#pragma omp parallel for schedule(dynamic)
	for (uint64_t i = 0; i < weights->size(); ++i) {
		std::string gzCommand = "tar xzf " + weights->at(i).second + " --skip-old-files";
		if (verbose) {
			std::cout << gzCommand + "\n";
		}
		system(gzCommand.c_str());
	}

	delete(sendBlocks);
	delete(localBlock);

	// Delete each tar.gz file
	#pragma omp parallel for schedule(static)
	for (uint64_t i = 0; i < weights->size(); ++i) {
		std::string gzRmCommand = weights->at(i).second;
		if (verbose) {
			std::cout << "remove(" + gzRmCommand + ")\n";
		}
		if (remove(gzRmCommand.c_str())) {
			std::cout << "ERROR: " + gzRmCommand + " could not be removed.\n";
		}
	}
	
	if (globalRank == root) {
		// Decided whether or not to keep the ptgz.tar archive
		if (!keep) {
			std::string tarRmCommand = name + ".ptgz.tar";
			if (verbose) {
				std::cout << "remove(" + tarRmCommand + ")\n";
			}
			if (remove(tarRmCommand.c_str())) {
				std::cout << "ERROR: " + tarRmCommand + " could not be removed.\n";
			}
		}
	}

	// End message passing and clean up
	MPI_Finalize();
	weights->clear();
	delete(weights);
}

char cwd [PATH_MAX];

// Checks to see if the user asks for help.
// Gathers the user provided settings for ptgz.
// Finds the number of files that need to be stored.
// Gathers the file paths of all files to be stored.
// Either compresses the files or extracts the ptgz.tar archive.
int main(int argc, char *argv[]) {
	// Start messsage passing
	printf("Message Passing\n");
	MPI_Init(NULL, NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &globalRank);
	MPI_Comm_size(MPI_COMM_WORLD, &globalSize);
	Settings *instance = new Settings;
	
	if (globalRank == root) {
		printf("Help Check\n");
		helpCheck(argc, argv);
	}
	printf("Get Settings\n");
	getSettings(argc, argv, instance);
	printf("Get CWD\n");
	getcwd(cwd, PATH_MAX);

	if ((*instance).compress) {
		std::vector<std::string> *filePaths = new std::vector<std::string>();
		if (globalRank == root) {
			printf("Get Paths\n");
			getPaths(filePaths, cwd, "");
		}
		MPI_Barrier(MPI_COMM_WORLD);
		printf("Compression\n");
		compression(filePaths, (*instance).name, (*instance).verbose, (*instance).verify, (*instance).levelSet, (*instance).level);
	} else {
		MPI_Barrier(MPI_COMM_WORLD);
		extraction((*instance).name, (*instance).verbose, (*instance).keep);
	}

	delete(instance);
	return 0;
}
