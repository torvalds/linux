LIBDIR := ../../../lib
BPFDIR := $(LIBDIR)/bpf

CFLAGS += -Wall -O2 -I../../../include/uapi -I$(LIBDIR)
LDLIBS += -lcap

TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map

TEST_PROGS := test_kmod.sh

include ../lib.mk

BPFOBJ := $(OUTPUT)/bpf.o

$(TEST_GEN_PROGS): $(BPFOBJ)

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/
