LIBDIR := ../../../lib
BPFDIR := $(LIBDIR)/bpf

CFLAGS += -Wall -O2 -I../../../include/uapi -I$(LIBDIR) -I../../../include
LDLIBS += -lcap -lelf

TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs

TEST_GEN_FILES = test_pkt_access.o

TEST_PROGS := test_kmod.sh

include ../lib.mk

BPFOBJ := $(OUTPUT)/libbpf.a

$(TEST_GEN_PROGS): $(BPFOBJ)

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/

CLANG ?= clang

%.o: %.c
	$(CLANG) -I../../../include/uapi -I../../../../samples/bpf/ \
		-D__x86_64__ -Wno-compare-distinct-pointer-types \
		-O2 -target bpf -c $< -o $@
