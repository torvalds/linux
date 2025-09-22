# $OpenBSD: Makefile,v 1.5 2020/07/25 12:44:50 bluhm Exp $
# $NetBSD: Makefile,v 1.5 2002/09/18 04:16:02 lukem Exp $

PROG=		extest
SRCS=		extest.c subr_extent.c
CPPFLAGS+=	-D_EXTENT_TESTING -DDIAGNOSTIC
CLEANFILES+=	extest.c *.tmp *.out *.norm

.PATH:		${.CURDIR}/../../../../sys/kern

LONG_MINUSONE !=	perl -e 'print unpack("H*",pack("l!",-1)),"\n"'

REGRESS_TARGETS =	run-extent

run-extent: ${PROG}
	./${PROG} >extest.out
	sed -n 's/ffffffffffffffff/${LONG_MINUSONE}/;5,$$p' \
	    ${.CURDIR}/extest.exp >extest.norm
	diff -up extest.norm extest.out

extest.c: extest.awk tests
	awk -f ${.CURDIR}/extest.awk ${.CURDIR}/tests >extest.c.tmp
	mv extest.c.tmp extest.c

.include <bsd.regress.mk>
