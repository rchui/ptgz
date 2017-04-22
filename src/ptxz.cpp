#include <iostream>
#include <unistd.h>
#include <dirent.h>

void helpCheck(char *argv[]) {
	if (argv[1] == std::string("-h") || argv[1] == std::string("--help")) {
		std::cout << "\nptxv - Parallel Tar XZ by Ryan Chui (2017)\n" << std::endl;
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

void getPaths(char **filePaths, const char *cwd, int *index) {
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
					getPaths(filePaths, filePath.c_str(), index);
				} else {
					filePaths[*index] = &filePath[0u];
					*index += 1;
				}
			}
		}
		closedir(dir);
	}
}

char cwd [PATH_MAX];

int main(int argc, char *argv[]) {
	int *numFiles = new int(0);
	
	if (argc > 1) {
		helpCheck(argv);
	}

	getcwd(cwd, PATH_MAX);
	findAll(numFiles, cwd);

	char **filePaths = (char**) malloc(sizeof(char*) * *numFiles);
	delete(numFiles);
	int *index = new int(0);

	getPaths(filePaths, cwd, index);
	
	free(filePaths);
	return 0;
}
