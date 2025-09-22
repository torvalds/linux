#	$OpenBSD: Makefile,v 1.9 2019/05/10 15:36:19 bluhm Exp $
#
# Author: Ingo Schwarze <schwarze@openbsd.org>, 2016.  Public Domain.

PROGS =		test_read_char test_getcmd
CLEANFILES +=	out_getcmd.txt

REGRESS_TARGETS += run-read_char
run-read_char: test_read_char
	sh ${.CURDIR}/test_read_char.sh

REGRESS_TARGETS += run-getcmd
run-getcmd: test_getcmd
	echo -n s | ./test_getcmd > out_getcmd.txt
	diff -u ${.CURDIR}/exp_getcmd.txt out_getcmd.txt

.include <bsd.regress.mk>
