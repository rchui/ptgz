#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <queue>
#include <utility>
#include "omp.h"

// Contains the various options the user can pass ptgz.
struct Settings {
	Settings(): extract(),
				compress(),
   				verbose(),
				keep(),
				verify(),
				output() {}
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
		std::cout << "    terrabyte sized directories into a single file. ptgz was developed at the National Center for \n";
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
		std::cout << "                                unpacked and split into its component files. <archive> should be the name of\n";
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
		} else if (arg == "-k") {
			(*instance).keep = true;
		} else if (arg == "-W") {
			(*instance).verify = true;
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
void compression(std::vector<std::string> *filePaths, std::string name, bool verbose, bool verify) {
	std::random_shuffle(filePaths->begin(), filePaths->end());
	unsigned long long filePathSize = filePaths->size();
	unsigned long long blockSize = (filePathSize / (omp_get_max_threads() * 10)) + 1;
	// std::vector<std::string> *tarNames = new std::vector<std::string>(filePathSize / blockSize + 1);
	std::vector<std::string> *tarNames = new std::vector<std::string>(filePathSize / blockSize);
	
	// Gzips the blocks of files into a single compressed file
	#pragma omp parallel for schedule(dynamic)
	for (int i = 0; i < omp_get_max_threads() * 10; ++i) {
		unsigned long long start = blockSize * i;
		if (start < filePathSize) {
			// Store the name of each file for a block owned by each thread.
			// Each thread will use the file to tar and gzip compress their block.
			std::ofstream tmp;
			tmp.open(std::to_string(i) + "." + name + ".ptgz.tmp", std::ios_base::app);
			std::string gzCommand = "GZIP=-1 tar -cz -T " + std::to_string(i) + "." + name + ".ptgz.tmp -f " + std::to_string(i) + "." + name + ".tar.gz";
			for (unsigned long long j = start; j < std::min(start + blockSize, filePathSize); ++j) {
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
	std::string tarCommand;
	if (!verify) {
		tarCommand = "tar -c -T " + name + ".ptgz.idx -f " + name + ".ptgz.tar";	
	} else {
		tarCommand = "tar -c -W -T " + name + ".ptgz.idx -f " + name + ".ptgz.tar";
	}
	for (unsigned long long i = 0; i < tarNames->size(); ++i) {
		idx << tarNames->at(i) + "\n";
	}
	idx << name + ".ptgz.idx\n";
	idx.close();
	
	if (verbose) {
		std::cout << tarCommand + "\n";
	}
	
	system(tarCommand.c_str());

	// Removes all temporary blocks.
	#pragma omp parallel for schedule(static)
	for (unsigned long long i = 0; i < tarNames->size(); ++i) {
		std::string rmCommand = tarNames->at(i);
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
	filePaths->clear();
	delete(filePaths);
}

unsigned long long GetFileSize(std::string filename)
	{
		struct stat stat_buf;
		unsigned long long rc = stat(filename.c_str(), &stat_buf);
		return rc == 0 ? stat_buf.st_size : -1;
	}

// Unpacks the archive.
// Reads in all the files from the index file.
// Unpacks each file.
// Deletes all temporary file blocks and header files.
// Parameters: filePaths (std::vector<std::string> *) holder for all file paths.
// 			   name (std::string) name of ptgz archive file.
// 			   verbose (bool) user option for verbose output.
// 			   keep (bool) user option for keeping ptgz archive.
void extraction(std::vector<std::string> *filePaths, std::string name, bool verbose, bool keep) {
	// Unpack the 1st layer tarball
	std::string exCommand = "tar xf " + name;
	if (verbose) {
		std::cout << exCommand + "\n";
	}
	system(exCommand.c_str());

	// Get the name from the name of the 1st layer tarball
	for (int i = 0; i < 9; ++i) {
		name.pop_back();
	}

	// Read in all tar.gz files form the ptgz.idx file
	// Delete the ptgz.idx file
	std::ifstream idx;
	std::string line;
	idx.open(name + ".ptgz.idx", std::ios_base::in);
	while (std::getline(idx, line)) {
		std::cout << line << std::endl;
		filePaths->push_back(line);
	}
	idx.close();
	std::string idxRmCommand = filePaths->back();
	if (remove(idxRmCommand.c_str())) {
		std::cout << "ERROR: " + idxRmCommand + " could not be removed.\n";	
	}
	filePaths->pop_back();

	std::vector<std::pair<unsigned long long, std::string>> *weights = new std::vector<std::pair<unsigned long long, std::string>>();

	std::cout << filePaths->size() << std::endl;
	for (unsigned long long i = 0; i < filePaths->size(); ++i) {
		weights->push_back(std::make_pair(GetFileSize(filePaths->at(i)), filePaths->at(i)));
	}
	for (int i = 0; i < weights->size(); ++i) {
		std::cout << weights->at(i).first << std::endl;
		std::cout << weights->at(i).second << std::endl;
	}

	// Unpack each tar.gz file.
	#pragma omp parallel for schedule(dynamic)
	for (unsigned long long i = 0; i < filePaths->size(); ++i) {
		std::string gzCommand = "tar xzf " + filePaths->at(i);
		if (verbose) {
			std::cout << gzCommand + "\n";
		}
		system(gzCommand.c_str());
	}

	// Double check unpacking.
	#pragma omp parallel for schedule(dynamic)
	for (unsigned long long i = 0; i < filePaths->size(); ++i) {
		std::string gzCommand = "tar xzf " + filePaths->at(i) + " --skip-old-files";
		if (verbose) {
			std::cout << gzCommand + "\n";
		}
		system(gzCommand.c_str());
	}

	// Delete each tar.gz file
	#pragma omp parallel for schedule(static)
	for (unsigned long long i = 0; i < filePaths->size(); ++i) {
		std::string gzRmCommand = filePaths->at(i);
		if (verbose) {
			std::cout << "remove(" + gzRmCommand + ")\n";
		}
		if (remove(gzRmCommand.c_str())) {
			std::cout << "ERROR: " + gzRmCommand + " could not be removed.\n";
		}
	}
	
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

	weights->clear();
	delete(weights);
	filePaths->clear();
	delete(filePaths);
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
		findAll(numFiles, cwd);
		getPaths(filePaths, cwd, "");
		compression(filePaths, (*instance).name, (*instance).verbose, (*instance).verify);
	} else {
		extraction(filePaths, (*instance).name, (*instance).verbose, (*instance).keep);
	}

	delete(instance);
	delete(numFiles);
	return 0;
}
