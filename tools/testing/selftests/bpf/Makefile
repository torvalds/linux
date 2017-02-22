LIBDIR := ../../../lib
BPFOBJ := $(LIBDIR)/bpf/bpf.o

CFLAGS += -Wall -O2 -lcap -I../../../include/uapi -I$(LIBDIR)

test_objs = test_verifier test_tag test_maps test_lru_map test_lpm_map

TEST_PROGS := $(test_objs) test_kmod.sh
TEST_FILES := $(test_objs)

.PHONY: all clean force

all: $(test_objs)

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(dir $(BPFOBJ))

$(test_objs): $(BPFOBJ)

include ../lib.mk

clean:
	$(RM) $(test_objs)
