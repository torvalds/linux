#	$OpenBSD: Makefile,v 1.2 2010/06/29 16:20:28 guenther Exp $

NOMAN=
PROG=orientation_test
CPPFLAGS+=-I${.CURDIR}/../../../../lib/libc

run-regress-${PROG}: ${PROG}
	./${PROG}

.include <bsd.regress.mk>
