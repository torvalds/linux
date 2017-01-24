CFLAGS += -Wall -O2 -I../../../../usr/include

test_objs = test_verifier test_tag test_maps test_lru_map test_lpm_map

TEST_PROGS := $(test_objs) test_kmod.sh
TEST_FILES := $(test_objs)

all: $(test_objs)

include ../lib.mk

clean:
	$(RM) $(test_objs)
