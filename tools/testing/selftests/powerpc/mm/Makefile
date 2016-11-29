noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := hugetlb_vs_thp_test subpage_prot prot_sao
TEST_GEN_FILES := tempfile

include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c

prot_sao: ../utils.c

tempfile:
	dd if=/dev/zero of=tempfile bs=64k count=1

