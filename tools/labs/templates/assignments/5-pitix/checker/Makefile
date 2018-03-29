#
# pitix filesystem - test Makefile
#

CFLAGS = -Wall -Wextra -g -m32
LDFLAGS = -static -m32

.PHONY: all

all: mkfs.pitix
	ln -sf _test/mkfs.pitix mkfs.pitix

mkfs.pitix: _test/
	make -C _test/

clean:
	-rm -f *~
	-rm -f mkfs.pitix
	make -C _test/ clean
