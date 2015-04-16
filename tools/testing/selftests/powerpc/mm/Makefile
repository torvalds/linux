noarg:
	$(MAKE) -C ../

TEST_PROGS := hugetlb_vs_thp_test subpage_prot

all: $(TEST_PROGS) tempfile

$(TEST_PROGS): ../harness.c

include ../../lib.mk

tempfile:
	dd if=/dev/zero of=tempfile bs=64k count=1

clean:
	rm -f $(TEST_PROGS) tempfile
