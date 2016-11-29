noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := hugetlb_vs_thp_test subpage_prot prot_sao
TEST_GEN_FILES := tempfile

include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c

$(OUTPUT)/prot_sao: ../utils.c

$(OUTPUT)/tempfile:
	dd if=/dev/zero of=$@ bs=64k count=1

