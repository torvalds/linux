#	$OpenBSD: Makefile,v 1.3 2021/10/06 12:43:14 bluhm Exp $

PROG=	exceptions
SRCS=	exceptions.cc
LDADD=	-lpthread
DPADD=	${LIBPTHREAD}

REGRESS_TARGETS=runs

runs: exceptions
	for i in $$(jot 100); do ./exceptions; done

.include <bsd.regress.mk>
