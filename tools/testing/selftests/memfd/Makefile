CC = $(CROSS_COMPILE)gcc
CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../../../include/uapi/
CFLAGS += -I../../../../include/
CFLAGS += -I../../../../usr/include/

all:
	$(CC) $(CFLAGS) memfd_test.c -o memfd_test

TEST_PROGS := memfd_test

include ../lib.mk

build_fuse:
	$(CC) $(CFLAGS) fuse_mnt.c `pkg-config fuse --cflags --libs` -o fuse_mnt
	$(CC) $(CFLAGS) fuse_test.c -o fuse_test

run_fuse: build_fuse
	@./run_fuse_test.sh || echo "fuse_test: [FAIL]"

clean:
	$(RM) memfd_test fuse_test
