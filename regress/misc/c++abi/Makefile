# $OpenBSD: Makefile,v 1.1 2013/07/06 23:06:36 espie Exp $

REGRESS_TARGETS = check1

check1: nm1.out
	diff -I 'OpenBSD' -u nm1.out ${.CURDIR}/nm1.ref

nm1.out: nm1.o
	nm -C nm1.o | fgrep witness | sort | sed -e 's/.* T witness_//' >$@

CLEANFILES = *.o *.out

.include <bsd.regress.mk>
