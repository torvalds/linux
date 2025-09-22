#	$OpenBSD: Makefile,v 1.2 2023/12/29 02:37:39 aisha Exp $

NAMED_TESTS = random-inserts sequential-inserts \
	sequential-removes random-removes remove-nfind \
	remove-pfind node-iterations iteration-macros insert-next \
	insert-prev benchmarks

REGRESS_TARGETS = all-tests ${NAMED_TESTS}

PROGS = rb-test rbt-test

all-tests: rb-test rbt-test
	@echo "===== testing RB macros ====="
	./rb-test
	@echo "===== testing RBT macros ====="
	./rbt-test

${NAMED_TESTS}: rb-test rbt-test
	@echo "===== testing RB macros ====="
	./rb-test $@
	@echo "===== testing RBT macros ====="
	./rbt-test $@

.include <bsd.regress.mk>
