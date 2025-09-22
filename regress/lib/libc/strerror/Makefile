#	$OpenBSD: Makefile,v 1.1 2004/04/30 17:15:12 espie Exp $

NOMAN=
PROG=strerror_test
CPPFLAGS+=-I${.CURDIR}/../../../../lib/libc
CLEANFILES+= invalid.out valid.out

run-regress-strerror_test: ${PROG}
	./${PROG} >${.OBJDIR}/valid.out
	cmp -s ${.OBJDIR}/valid.out ${.CURDIR}/valid.ok

.include <bsd.regress.mk>
