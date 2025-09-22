#	$OpenBSD: Makefile,v 1.3 2002/09/02 20:01:44 avsm Exp $

PROG=	sigfpe

# XXX - don't want the compiler to optimize away our tests
CFLAGS=   -O0 ${PIPE} ${DEBUG}

REGRESS_TARGETS=	divzero oflow

divzero: ${PROG}
	./${PROG} -z

oflow: ${PROG}
	./${PROG} -o

.include <bsd.regress.mk>
