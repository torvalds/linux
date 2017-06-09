CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../../../include/uapi/
CFLAGS += -I../../../../include/
CFLAGS += -I../../../../usr/include/

TEST_PROGS := run_fuse_test.sh
TEST_GEN_FILES := memfd_test fuse_mnt fuse_test

fuse_mnt.o: CFLAGS += $(shell pkg-config fuse --cflags)
fuse_mnt: LDFLAGS += $(shell pkg-config fuse --libs)

include ../lib.mk

