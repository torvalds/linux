LIBDIR := ../../../lib
BPFOBJ := $(LIBDIR)/bpf/bpf.o

CFLAGS += -Wall -O2 -lcap -I../../../include/uapi -I$(LIBDIR) $(BPFOBJ)

TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map

TEST_PROGS := test_kmod.sh

all: $(TEST_GEN_PROGS)

.PHONY: all clean force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(dir $(BPFOBJ))

$(test_objs): $(BPFOBJ)

include ../lib.mk
