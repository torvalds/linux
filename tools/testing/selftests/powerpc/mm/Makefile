noarg:
	$(MAKE) -C ../

TEST_PROGS := hugetlb_vs_thp_test subpage_prot
TEST_FILES := tempfile

all: $(TEST_PROGS) $(TEST_FILES)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

tempfile:
	dd if=/dev/zero of=tempfile bs=64k count=1

clean:
	rm -f $(TEST_PROGS) tempfile
