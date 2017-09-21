#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <queue>
#include <utility>
#include <limits>

#include "mpitar.hh"

#include "omp.h"
#include "mpi.h"

int root = 0;
int globalRank, globalSize;

// Contains the various options the user can pass ptgz.
// Members: 
//	    extract (bool) whether ptgz should be extracting.
//	    compress (bool) whether ptgz should be compressing.
//	    verbose (bool) whether ptgz should output commands.
//	    keep (bool) whether ptgz should keep the extracted arvhive.
//	    output (bool) whether archive name has been given.
//	    verify (bool) whether ptgz should verify the compressed archive.
//	    name (std::string) name of archive to make or extract.
struct Settings {
	Settings(): extract(),
				compress(),
				verbose(),
				keep(),
				output(),
				verify(),
				name() {}
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
		std::cout << "    -l    Set Level             Instruct ptgz to use a specific compression level. Value must be from 1 to 9;\n";
		std::cout << "                                1 is low compression, fast speed and 9 is high compression, low speed.\n" << std::endl;
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
				perror("ERROR: ptgz cannot both compress and extract. \"ptgz -h\" for help.\n");
				exit(1);
			}
			(*instance).extract = true;
		} else if (arg == "-c") {
			if ((*instance).extract) {
				perror("ERROR: ptgz cannot both compress and extract. \"ptgz -h\" for help.\n");
				exit(1);
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
			settings.pop();
			int64_t level = std::stoi(settings.front());
			if (level >= 1 && level <= 9) {
				if (setenv("GZIP", ("-" + settings.front()).c_str(), 1) < 0) {
					perror("ERROR: GZIP could not be set.\n");
					exit(1);
				}
			} else {
				perror("ERROR: level must be set from 1 to 9.\n");
				exit(1);
			}
		} else {
			if (settings.size() > 1) {
				perror("ERROR: ptgz was called incorrectly. \"ptgz -h\" for help.\n");
				exit(1);
			}
			(*instance).output = true;
			(*instance).name = arg;
		}

		settings.pop();
	}

	if (!(*instance).output) {
		perror("ERROR: No output file name given. \"ptgz -h\" for help.\n");
		exit(1);
	} else if ((*instance).keep && !(*instance).extract) {
		perror("ERROR: Can't use keep option without extract. \"ptgz -h\" for help.\n");
	}
}

bool isLink(std::string fileName) {
	struct stat buf;
	int x;
	const char* filePtr = fileName.c_str();

	x = lstat (filePtr, &buf);
	if (S_ISLNK(buf.st_mode)) {
		return true;
	} else {
		return false;
	}
}

// Gets and returns the size of a file
// Parameters: filename (std::string) name of the file whose size to find.
uint64_t getFileSize(std::string fileName) {
		try {
			const char *filePtr = fileName.c_str();
			struct stat st;
			if (stat(filePtr, &st) != 0) {
				return 0;
			}
			return static_cast<uint64_t>(st.st_size);
		} catch(...) {
			return 0;
		}
	}

// Gets the paths for all files in the space to store.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   cwd (const char *) current working directory.
// 			   rootPath (std::string) path from the root of the directory to be stored.
void getPaths(std::vector<std::pair<uint64_t, std::string>> *filePaths, const char *cwd, std::string rootPath) {
	DIR *dir;
	struct dirent *ent;

	// Check if cwd is a directory
	if ((dir = opendir(cwd)) != NULL) {
		// Get all file paths within directory.
		int64_t num = 0;
		while ((ent = readdir (dir)) != NULL) {
			std::string fileBuff = std::string(ent -> d_name);
			if (fileBuff != "." && fileBuff != "..") {
				if (num == 0) {
					num++;
				}
				DIR *dir2;
				std::string filePath = std::string(cwd) + "/" + fileBuff;
				// Check if file path is a directory.
				if ((dir2 = opendir(filePath.c_str())) != NULL) {
					closedir(dir2);
					if (isLink(filePath)) {
						filePaths->push_back(std::make_pair(0, rootPath + fileBuff));
					} else {
						getPaths(filePaths, filePath.c_str(), rootPath + fileBuff + "/");
					}
				} else {
					std::string fullPath = rootPath + fileBuff;
					filePaths->push_back(std::make_pair(getFileSize(fullPath), fullPath));
				}
			}
		}
		if (num == 0) {
			filePaths->push_back(std::make_pair(0, rootPath));
		}
		closedir(dir);
	}
}

// Makes manual extraction script.
// Parameters: name (std::string) name of the ptgz archive.
void makeScript(std::string name) {
	std::ofstream script (name + ".sh");
	if (script.is_open()) {
		script << "#!/bin/bash\n";
		script << "\n";
		script << "for TARGZ in *.ptgz.tar.gz\n";
		script << "do\n";
		script << "    tar xzf $TARGZ\n";
		script << "    rm $TARGZ\n";
		script << "done\n";
		script << "\n";
		script << "rm *.ptgz.idx\n";
	} else {
		std::cout << "ERROR: Could not make script file\n";
		exit(0);
	}
}

// Prints the command to be executed.
// Parameters: ocmmand (char *const[]) command to be executed.
//             length (int) number of elements in the command.
void printCommand(char *const command[], int length) {
	std::string output = command[0];
	for (int i = 1; i < length; ++i) {
		output += " " + std::string(command[i]);
	}
	std::cout << output + "\n";
}

// Converts strings to char* for C functions.
// Parameters: input (std::string) string to be converted.
char* strToChar(std::string input) {
	char *tmp = new char[input.length() + 1];
	strcpy(tmp, input.c_str());
	return tmp;
}

// Spawns child process which executes a system command.
// Parent waits until child dies.
// Parameters: command (const char *) command to be executed.
int execute(char *const command[]) {
	int status;
	pid_t childPid;

	switch (childPid = fork()) {
		case -1:
			status = -1;
			break;
		case 0:
			execvp(command[0], command);
			_exit(1);
		default:
			while (waitpid(childPid, &status, 0) == -1) {
				if (errno != EINTR) {
					status = -1;
					break;
				}
			}
	}

	return status;
}

// Divides files into blocks.
// Compresses each block into a single file.
// Combines all compressed blocks into a single file.
// Removes temporary blocks and header files.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   name (std::string) user given name for storage file.
// 			   verbose (bool) user option for verbose output.
//			   verify (bool) user option for tar archive verification.
void compression(std::vector<std::pair<uint64_t, std::string>> *filePaths, std::string name, bool verbose, bool verify, int numThreads) {
	if (globalRank == root) {
		std::sort(filePaths->rbegin(), filePaths->rend());
	}

	// Send the total number of files to all ranks.
	uint64_t filePathSize;
	if (globalRank == root) {
		filePathSize = filePaths->size();
	}
	MPI_Bcast(&filePathSize, 1, MPI_UINT64_T, root, MPI_COMM_WORLD);
	std::vector<std::string> *tarNames;

	int64_t numBlocks = numThreads * 10 * globalSize;
	uint64_t blockSize;
	int64_t *sendSizes = new int64_t[globalSize * 2];
	int64_t reserved = 0;
	int64_t tarBlock;
	int64_t *localSize = new int64_t[2];

	// Get blockSize and set tarName vector size.
	if (filePathSize % numBlocks == 0) {
		blockSize = (filePathSize / numBlocks);
	} else {
		blockSize = (filePathSize / numBlocks) + 1;
	}

	if (filePathSize % blockSize == 0) {
		tarNames = new std::vector<std::string>(filePathSize / blockSize);
	} else {
		tarNames = new std::vector<std::string>(filePathSize / blockSize + 1);
	}

	if (globalRank == root) {
		// Write all files to text files.
		#pragma omp parallel for schedule(static)
		for (uint64_t i = 0; i < tarNames->size(); ++i) {
			if (i < filePaths->size()) {
				std::ofstream tmp;
				tmp.open(std::to_string(i) + "." + name + ".ptgz.tmp", std::ios_base::app);
				for (uint64_t j = i; j < filePaths->size(); j += tarNames->size()) {
					tmp << filePaths->at(j).second + "\n";
				}
				tmp.close();
				tarNames->at(i) = std::to_string(i) + "." + name + ".ptgz.tar.gz";
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

	// Send blocks to all ranks
	sync();
	MPI_Scatter(sendSizes, 2, MPI_INT64_T, localSize, 2, MPI_INT64_T, root, MPI_COMM_WORLD);

	// Write tar index file
	for (int64_t i = 0; i < globalSize; ++i) {
		if (globalRank == i) {
			std::ofstream oFile(name + ".idx", std::ios::out | std::ios::app);
			for (int64_t j = localSize[0]; j < localSize[0] + localSize[1]; ++j) {
				oFile << "---- " + std::to_string(j) + "." + name + ".ptgz.tar.gz ----\n\n";
				std::ifstream iFile(std::to_string(j) + "." + name + ".ptgz.tmp", std::ios::in);
				if (iFile.is_open()) {
					oFile << iFile.rdbuf();
				} else {
					std::cout << "ERROR: Could not add block " + std::to_string(j) + "\n";
				}
				oFile << "\n";
			}
			oFile.close();
		}
		MPI_Barrier(MPI_COMM_WORLD);
	}

	// Build tar archives for each block; largest to smallest.
	#pragma omp parallel for schedule(dynamic)
	for (int64_t i = 0; i < localSize[1] + 1; ++i) {
		int64_t archiveNum;
		if (i % 2) {
			archiveNum = (globalSize - (globalRank + 1)) + globalSize * i;
		} else {
			archiveNum = globalRank + globalSize * i;
		}
		if (archiveNum < tarNames->size()) {
			char* const gzCommand[] = {
				"tar",
				"-c",
				"-z",
				"-T",
				strToChar(std::to_string(archiveNum) + "." + name + ".ptgz.tmp"),
				"-f",
				strToChar(std::to_string(archiveNum) + "." + name + ".ptgz.tar.gz"),
				(char *) NULL
			};
			if (verbose) {
				printCommand(gzCommand, 7);
			}
			execute(gzCommand);
			delete[] gzCommand[4];
			delete[] gzCommand[6];
		}
	}

	sync();
	MPI_Barrier(MPI_COMM_WORLD);

	if (globalRank == root) {
		// Combines gzipped blocks together into a single tarball.
		// Write tarball names into an idx file for extraction.
		std::ofstream idx, tmp;
		idx.open(name + ".ptgz.idx", std::ios_base::app);
		char* const tarCommand[] = {
			"mpirun",
			"-np",
			strToChar(std::to_string(numThreads * globalSize - globalSize)),
			"mpitar",
			"-c",
			"-f",
			strToChar(name + ".ptgz.tar"),
			"-T",
			strToChar(name + ".ptgz.idx"),
			(char *) NULL
		};
		for (uint64_t i = 0; i < tarNames->size(); ++i) {
			idx << tarNames->at(i) + "\n";
		}
		idx << name + ".ptgz.idx\n";
		makeScript(name);
		idx << name + ".sh\n";
		idx << name + ".idx\n";
		idx.close();
		
		if (verbose) {
			printCommand(tarCommand, 9);
		}
	
		// TODO:
		char ** mpitarArray = {"mpitar", "-c", "-f", strToChar(name + ".ptgz.tar"), "-T", strToChar(name + ".ptgz.tar")};

		mpitar(6, &mpitarArray);
		// execute(tarCommand);
		delete[] tarCommand[2];
		delete[] tarCommand[6];
		delete[] tarCommand[8];
	}

	sync();
	MPI_Barrier(MPI_COMM_WORLD);

	// Removes all temporary blocks.
	#pragma omp parallel for schedule(static)
	for (uint64_t i = localSize[0]; i < localSize[0] + localSize[1]; ++i) {
		std::string rmCommand = std::to_string(i) + "." + name + ".ptgz.tar.gz";
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
		if (remove((name + ".sh").c_str())) {
			std::cout << "ERROR: " + name + ".sh could not be removed.\n";
		}
		if (remove((name + ".idx").c_str())) {
			std::cout << "ERROR: " + name + ".idx could not be removed.\n";
		}
		if (remove((name + ".ptgz.tar.idx").c_str())) {
			std::cout << "ERROR: " + name + ".ptgz.tar.idx could not be removed\n";
		}

		tarNames->clear();
		delete(tarNames);
	}
	MPI_Finalize();
}

// Unpacks the archive.
// Reads in all the files from the index file.
// Unpacks each file.
// Deletes all temporary file blocks and header files.
// Parameters: name (std::string) name of ptgz archive file.
// 			   verbose (bool) user option for verbose output.
// 			   keep (bool) user option for keeping ptgz archive.
void extraction(std::string name, bool verbose, bool keep, int numThreads) {
	// Get the name from the name of the 1st layer tarball
	for (int64_t i = 0; i < 9; ++i) {
		name.pop_back();
	}

	int64_t numArchives;

	if (globalRank == root) {
		// Unpack index from the 1st layer tar ball
		char* const exCommand[] = {
			"tar",
			"-x",
			"-f",
			strToChar(name + ".ptgz.tar"),
			strToChar(name + ".ptgz.idx"),
			(char *) NULL
		};
		if (verbose) {
			printCommand(exCommand, 5);
		}
		execute(exCommand);
		delete[] exCommand[3];
		delete[] exCommand[4];

		// Get number of archives and delete index.
		std::ifstream idx;
		std::string line;
		numArchives = 0;
		idx.open(name + ".ptgz.idx", std::ios_base::in);
		while (std::getline(idx, line)) {
			++numArchives;
		}
		numArchives -= 3; // There are three extra files in the index file.
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
		char* const tarCommand[] = {
			"tar",
			"-x",
			"-f",
			strToChar(name + ".ptgz.tar"),
			strToChar(std::to_string(i) + "." + name + ".ptgz.tar.gz"),
			(char *) NULL
		};
		execute(tarCommand);
		delete[] tarCommand[3];
		delete[] tarCommand[4];
	}

	sync();
	MPI_Barrier(MPI_COMM_WORLD);

	// Fill weights vector and sort by file size descending
	std::vector<std::pair<uint64_t, std::string>> *weights = new std::vector<std::pair<uint64_t, std::string>>(localBlock[1]);
	#pragma omp parallel for schedule(static)
	for (uint64_t i = 0; i < localBlock[1]; ++i) {
		std::string archiveName = std::to_string(i + localBlock[0]) + "." + name + ".ptgz.tar.gz";
		weights->at(i) = std::make_pair(getFileSize(archiveName), archiveName);
	}
	std::sort(weights->rbegin(), weights->rend());

	// Unpack each .ptgz.tar.gz file.
	#pragma omp parallel for schedule(dynamic)
	for (uint64_t i = 0; i < weights->size(); ++i) {
		char* const gzCommand[] = {
			"tar",
			"-x",
			"-z",
			"--null",
			"-f",
			strToChar(weights->at(i).second),
			(char *) NULL
		};
		if (verbose) {
			printCommand(gzCommand, 6);
		}
		execute(gzCommand);
		delete[] gzCommand[5];
	}

	// Double check unpacking.
	#pragma omp parallel for schedule(dynamic)
	for (uint64_t i = 0; i < weights->size(); ++i) {
		// std::string gzCommand = "tar -x -k -z --null -f " + weights->at(i).second;
		char* const gzCommand[] = {
			"tar",
			"-x",
			"-k",
			"-z",
			"--null",
			"-f",
			strToChar(weights->at(i).second),
			(char *) NULL
		};
		if (verbose) {
			printCommand(gzCommand, 7);
		}
		execute(gzCommand);
		delete[] gzCommand[6];
	}

	delete(sendBlocks);
	delete(localBlock);

	// Delete each .ptgz.tar.gz file
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
	MPI_Init(NULL, NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &globalRank);
	MPI_Comm_size(MPI_COMM_WORLD, &globalSize);
	Settings *instance = new Settings;
	int numThreads = omp_get_max_threads();
	omp_set_num_threads(numThreads);
	
	if (globalRank == root) {
		helpCheck(argc, argv);
	}
	getSettings(argc, argv, instance);
	getcwd(cwd, PATH_MAX);

	if ((*instance).compress) {
		std::vector<std::pair<uint64_t, std::string>> *filePaths = new std::vector<std::pair<uint64_t, std::string>>();
		if (globalRank == root) {
			getPaths(filePaths, cwd, "");
		}
		MPI_Barrier(MPI_COMM_WORLD);
		compression(filePaths, (*instance).name, (*instance).verbose, (*instance).verify, numThreads);
	} else {
		MPI_Barrier(MPI_COMM_WORLD);
		extraction((*instance).name, (*instance).verbose, (*instance).keep, numThreads);
	}

	delete(instance);
	return 0;
}
