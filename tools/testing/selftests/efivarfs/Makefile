CFLAGS = -Wall

test_objs = open-unlink create-read

all: $(test_objs)

TEST_PROGS := efivarfs.sh
TEST_FILES := $(test_objs)

include ../lib.mk

clean:
	rm -f $(test_objs)
