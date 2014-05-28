noarg:
	$(MAKE) -C ../

PROGS := hugetlb_vs_thp_test

all: $(PROGS)

$(PROGS): ../harness.c

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS)

.PHONY: all run_tests clean
