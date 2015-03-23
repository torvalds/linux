noarg:
	$(MAKE) -C ../

PROGS := hugetlb_vs_thp_test subpage_prot

all: $(PROGS) tempfile

$(PROGS): ../harness.c

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

tempfile:
	dd if=/dev/zero of=tempfile bs=64k count=1

clean:
	rm -f $(PROGS) tempfile

.PHONY: all run_tests clean
