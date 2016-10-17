CFLAGS += -Wall -O2

test_objs = test_verifier test_maps

TEST_PROGS := test_verifier test_maps test_kmod.sh
TEST_FILES := $(test_objs)

all: $(test_objs)

include ../lib.mk

clean:
	$(RM) $(test_objs)
