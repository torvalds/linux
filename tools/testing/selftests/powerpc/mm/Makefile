# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := hugetlb_vs_thp_test subpage_prot prot_sao segv_errors wild_bctr \
		  large_vm_fork_separation bad_accesses
TEST_GEN_PROGS_EXTENDED := tlbie_test
TEST_GEN_FILES := tempfile

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c

$(OUTPUT)/prot_sao: ../utils.c

$(OUTPUT)/wild_bctr: CFLAGS += -m64
$(OUTPUT)/large_vm_fork_separation: CFLAGS += -m64
$(OUTPUT)/bad_accesses: CFLAGS += -m64

$(OUTPUT)/tempfile:
	dd if=/dev/zero of=$@ bs=64k count=1

$(OUTPUT)/tlbie_test: LDLIBS += -lpthread
