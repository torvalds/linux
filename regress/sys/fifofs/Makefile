#	$OpenBSD: Makefile,v 1.3 2021/07/10 16:32:01 mpi Exp $

PROG=fifotest
FIFO=fi.fo.fum
CLEANFILES+=${FIFO}
CFLAGS+= -Wall

REGRESS_TARGETS=runtest

runtest: ${PROG}
	./${PROG} ${FIFO} | diff -u ${.CURDIR}/${PROG}.out -

.include <bsd.regress.mk>
