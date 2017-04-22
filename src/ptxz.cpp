#include <iostream>
#include <unistd.h>

void helpCheck(char *argv[]) {
	if (argv[1] == std::string("-h") || argv[1] == std::string("--help")) {
		std::cout << "\nptxv - Parallel Tar XZ by Ryan Chui (2017)\n" << std::endl;
		exit(0);
	}
}



char cwd [PATH_MAX];

int main(int argc, char *argv[]) {
	helpCheck(argv);
	getcwd(cwd, PATH_MAX);
	return 0;
}
