#	$OpenBSD: Makefile,v 1.11 2021/09/27 18:47:46 bluhm Exp $
#	$NetBSD: Makefile,v 1.11 1995/12/12 01:54:15 cgd Exp $

PROG=	dbtest

# add -DSTATISTICS to CFLAGS to get usage statistics.  Note that
# for this to work, libc must be compiled with -DSTATISTICS as well
CFLAGS+= -g -D__DBINTERFACE_PRIVATE -DDEBUG
CLEANFILES+= t1 t2 t3 log

DBTARGETS=1 2 3 4 5 6 7 8 9 10 11 12 13 20

.for DT in ${DBTARGETS}
db-${DT}: ${PROG}
	sh ${.CURDIR}/run.test ${DT}
REGRESS_TARGETS+=db-${DT}
.PHONY: db-${DT}
.endfor

.include <bsd.regress.mk>
