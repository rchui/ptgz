#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <queue>
#include "omp.h"

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

void getSettings(int argc, char *argv[], Settings *instance) {
	std::queue<std::string> settings;
	for (int i = 1; i < argc; ++i) {
		settings.push(argv[i]);
	}
	
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

void compression(std::vector<std::string> *filePaths, std::string name) {
	unsigned long long int filePathSize = filePaths->size();
	unsigned long long int blockSize = (filePathSize / (omp_get_max_threads() * 10)) + 1;
	std::vector<std::string> *tarNames = new std::vector<std::string>(filePaths->size());
	
	std::cout << "3.1 Gzipping Blocks" << std::endl;
	#pragma omp parallel for schedule(dynamic)
	for (int i = 0; i < omp_get_max_threads() * 10; ++i) {
		unsigned long long int start = blockSize * i;
		if (start < filePathSize) {
			std::string gzCommand = "GZIP=-1 tar czvf " + name +"." + std::to_string(i) + ".tar.gz";
			for (unsigned long long int j = start; j < std::min(start + blockSize, filePathSize); ++j) {
				gzCommand += " " + filePaths->at(j);
			}
			std::cout << gzCommand + "\n";
			system(gzCommand.c_str());

			tarNames->at(i) = name + "." + std::to_string(i) + ".tar.gz";
		}
	}

	std::ofstream idx;
	idx.open(name + ".ptgz.idx", std::ios_base::app);

	std::cout << "3.2 Combining Blocks Together" << std::endl;
	std::string tarCommand = "tar cvf " + name + ".ptgz.tar";
	for (int i = 0; i < tarNames->size(); ++i) {
		tarCommand += " " + tarNames->at(i);
		idx << tarNames->at(i) + "\n";
	}
	idx.close();
	tarCommand += " " + name + ".ptgz.idx";
	std::cout << tarCommand + "\n";
	system(tarCommand.c_str());

	std::cout << "3.3 Removing Temporary Blocks" << std::endl;
	std::string rmCommand = "rm";
	for (int i = 0; i < tarNames->size(); ++i) {
		rmCommand += " " + tarNames->at(i);
	}
	rmCommand += " " + name + ".ptgz.idx";
	system(rmCommand.c_str());

	tarNames->clear();
	delete(tarNames);
}

void extraction() {

}

char cwd [PATH_MAX];

int main(int argc, char *argv[]) {
	Settings *instance = new Settings;
	int *numFiles = new int(0);
	std::vector<std::string> *filePaths = new std::vector<std::string>();
	
	helpCheck(argc, argv);
	getSettings(argc, argv, instance);
	getcwd(cwd, PATH_MAX);
	std::cout << "1. Searching File Tree" << std::endl;
	findAll(numFiles, cwd);
	std::cout << "2. Gathering Files" << std::endl;
	getPaths(filePaths, cwd, "");

	if ((*instance).compress) {
		std::cout << "3. Starting File Compression" << std::endl;
		compression(filePaths, (*instance).name);
	} else {
		extraction();
	}

	std::cout << "4. Cleaning Up" << std::endl;
	delete(instance);
	delete(numFiles);
	filePaths->clear();
	delete(filePaths);
	return 0;
}
