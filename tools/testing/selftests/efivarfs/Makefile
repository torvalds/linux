CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

test_objs = open-unlink create-read

all: $(test_objs)

TEST_PROGS := efivarfs.sh

include ../lib.mk

clean:
	rm -f $(test_objs)
