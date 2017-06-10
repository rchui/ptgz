CC=mpiCC
# we call getgrgid which requires a dynamic executable and SEGFAULTs for a
# statically linked one
export CRAYPE_LINK_TYPE=dynamic
export XTPE_LINK_TYPE=dynamic

.PHONY: clean all

all: mpitar test

mpitar: mpitar.cc timer.hh tarentry.cc tarentry.hh fileentry.hh cmdline.cc cmdline.hh
	$(CC) $(LDFLAGS) -Wall -g3 -Og mpitar.cc tarentry.cc cmdline.cc -o mpitar

test: mpitar test.sh
	@rm -rf test
	./test.sh || cat test/test.log

clean:
	rm -f mpitar *~
	rm -rf test
