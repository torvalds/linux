#	$OpenBSD: Makefile,v 1.7 2017/07/27 15:08:37 bluhm Exp $

PROG =		atexit_test
CPPFLAGS =	-I${.CURDIR}/../../../../lib/libc
CLEANFILES =	invalid.out valid.out
LDADD =		-static

run-regress-${PROG}: ${PROG}
	./${PROG} -valid 2>${.OBJDIR}/valid.out
	cmp -s ${.OBJDIR}/valid.out ${.CURDIR}/valid.ok
	./${PROG} -invalid-atexit 2>${.OBJDIR}/invalid.out
	cmp -s ${.OBJDIR}/invalid.out ${.CURDIR}/invalid.ok
	./${PROG} -invalid-cleanup 2>${.OBJDIR}/invalid.out
	cmp -s ${.OBJDIR}/invalid.out ${.CURDIR}/invalid.ok

.include <bsd.regress.mk>
