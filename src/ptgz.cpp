#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <queue>
#include "omp.h"

// Contains the various options the user can pass ptgz.
struct Settings {
	Settings(): extract(),
				compress(),
   				verbose(),
				output() {}
	bool extract;
	bool compress;
	bool verbose;
	bool output;
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
	for (int i = 1; i < argc; ++i) {
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
	}
}

// Finds the number of files in the space to store.
// Parameters: numFiles (int *) number of files.
// 			   cwd (const char *) current working directory.
void findAll(int *numFiles, const char *cwd) {
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
					findAll(numFiles, filePath.c_str());
				} else {
					*numFiles += 1;
				}
			}
		}
		closedir(dir);
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

// Divides files into blocks.
// Compresses each block into a single file.
// Combines all compressed blocks into a single file.
// Removes temporary blocks and header files.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   name (std::string) user given name for storage file.
// 			   verbose (bool) user option for verbose output.
void compression(std::vector<std::string> *filePaths, std::string name, bool verbose) {
	std::random_shuffle(filePaths->begin(), filePaths->end());
	unsigned long long int filePathSize = filePaths->size();
	unsigned long long int blockSize = (filePathSize / (omp_get_max_threads() * 10)) + 1;
	std::vector<std::string> *tarNames = new std::vector<std::string>(filePaths->size());
	
	// Gzips the blocks of files into a single compressed file
	std::cout << "3.1 Gzipping Blocks" << std::endl;
	#pragma omp parallel for schedule(dynamic)
	for (int i = 0; i < omp_get_max_threads() * 10; ++i) {
		unsigned long long int start = blockSize * i;
		if (start < filePathSize) {
			// Store the name of each file for a block owned by each thread.
			// Each thread will use the file to tar and gzip compress their block.
			std::ofstream tmp;
			tmp.open(std::to_string(i) + "." + name + ".ptgz.tmp", std::ios_base::app);
			std::string gzCommand = "GZIP=-1 tar -cz -T " + std::to_string(i) + "." + name + ".ptgz.tmp -f " + std::to_string(i) + "." + name + ".tar.gz";
			for (unsigned long long int j = start; j < std::min(start + blockSize, filePathSize); ++j) {
				tmp << filePaths->at(j) + "\n";
			}
	
			if (verbose) {
				std::cout << gzCommand + "\n";
			}

			tmp.close();
			system(gzCommand.c_str());
			tarNames->at(i) = std::to_string(i) + "." + name + ".tar.gz";
		}
	}

	// Combines gzipped blocks together into a single tarball.
	// Write tarball names into an idx file for extraction.
	std::ofstream idx, tmp;
	idx.open(name + ".ptgz.idx", std::ios_base::app);
	std::cout << "3.2 Combining Blocks Together" << std::endl;
	std::string tarCommand = "tar -c -T " + name + ".ptgz.idx -f " + name + ".ptgz.tar";
	for (unsigned long long int i = 0; i < tarNames->size(); ++i) {
		idx << tarNames->at(i) + "\n";
	}
	idx << name + ".ptgz.idx" + "\n";
	idx.close();
	
	if (verbose) {
		std::cout << tarCommand + "\n";
	}
	
	system(tarCommand.c_str());

	// Removes all temporary blocks and idx file.
	std::cout << "3.3 Removing Temporary Blocks" << std::endl;
	#pragma omp parallel for schedule(static)
	for (unsigned long long int i = 0; i < tarNames->size(); ++i) {
		std::string rmCommand = tarNames->at(i);
		if (verbose) {
			std::cout << "remove(" + rmCommand + ")\n";
		}
		remove(rmCommand.c_str());

		rmCommand = std::to_string(i) + "." + name + ".ptgz.tmp";
		if (verbose) {
			std::cout << "remove(" + rmCommand + ")\n";
		}
		remove(rmCommand.c_str());
	}

	std::string rmCommand;
	if (verbose) {
		std::cout << "remove(" + name + ".ptgz.idx)\n";
	}
	rmCommand = name + ".ptgz.idx";
	remove(rmCommand.c_str());

	tarNames->clear();
	delete(tarNames);
}

void extraction() {

}

char cwd [PATH_MAX];

// Checks to see if the user asks for help.
// Gathers the user provided settings for ptgz.
// Finds the number of files that need to be stored.
// Gathers the file paths of all files to be stored.
// Either compresses the files or extracts the ptgz.tar archive.
int main(int argc, char *argv[]) {
	Settings *instance = new Settings;
	int *numFiles = new int(0);
	std::vector<std::string> *filePaths = new std::vector<std::string>();
	
	helpCheck(argc, argv);
	getSettings(argc, argv, instance);
	getcwd(cwd, PATH_MAX);

	if ((*instance).compress) {
		std::cout << "1.  Searching File Tree" << std::endl;
		findAll(numFiles, cwd);
		std::cout << "2.  Gathering Files" << std::endl;
		getPaths(filePaths, cwd, "");
		std::cout << "3.  Starting File Compression" << std::endl;
		compression(filePaths, (*instance).name, (*instance).verbose);
	} else {
		extraction();
	}

	std::cout << "4.  Cleaning Up" << std::endl;
	delete(instance);
	delete(numFiles);
	filePaths->clear();
	delete(filePaths);
	return 0;
}
