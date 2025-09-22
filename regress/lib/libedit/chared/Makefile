#	$OpenBSD: Makefile,v 1.5 2017/07/05 15:31:45 bluhm Exp $
#
# Author: Ingo Schwarze <schwarze@openbsd.org>, 2016.  Public Domain.

PROG =		test_gets
CLEANFILES +=	out_gets.utf8 out_gets.ascii

run-regress-${PROG}:
	LC_ALL=en_US.UTF-8 ./test_gets < ${.CURDIR}/inp_gets.txt > out_gets.utf8
	diff -au ${.CURDIR}/exp_gets.utf8 out_gets.utf8
	LC_ALL=C ./test_gets < ${.CURDIR}/inp_gets.txt > out_gets.ascii
	diff -u ${.CURDIR}/exp_gets.ascii out_gets.ascii

.include <bsd.regress.mk>
