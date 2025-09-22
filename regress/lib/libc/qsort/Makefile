#       $OpenBSD: Makefile,v 1.3 2017/05/22 17:16:43 millert Exp $

PROG=	qsort_test
SRCS=	qsort_test.c antiqsort.c
CFLAGS+=-Wall

qsort-regress: ${PROG}
	./${PROG}

REGRESS_TARGETS=qsort-regress
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
