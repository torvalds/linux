LIBDIR := ../../../lib
BPFDIR := $(LIBDIR)/bpf
APIDIR := ../../../include/uapi
GENDIR := ../../../../include/generated
GENHDR := $(GENDIR)/autoconf.h

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

CFLAGS += -Wall -O2 -I$(APIDIR) -I$(LIBDIR) -I$(GENDIR) $(GENFLAGS) -I../../../include
LDLIBS += -lcap -lelf

TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align

TEST_GEN_FILES = test_pkt_access.o test_xdp.o test_l4lb.o test_tcp_estats.o test_obj_id.o \
	test_pkt_md_access.o

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
	$(CLANG) -I. -I./include/uapi -I../../../include/uapi \
		-Wno-compare-distinct-pointer-types \
		-O2 -target bpf -c $< -o $@
