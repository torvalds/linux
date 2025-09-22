# $OpenBSD: Makefile.inc,v 1.3 2020/12/26 16:37:54 bluhm Exp $

EXPATDIR=	${.CURDIR}/../../../../lib/libexpat
.PATH:		${EXPATDIR}/tests ${EXPATDIR}/tests/benchmark
PROG=		${.CURDIR:T}
CFLAGS=		-I${EXPATDIR}/lib -I${EXPATDIR} -DHAVE_EXPAT_CONFIG_H
CPPFLAGS=	-I${EXPATDIR}/lib -I${EXPATDIR} -DHAVE_EXPAT_CONFIG_H
LDADD=		-lexpat
DPADD=		${LIBEXPAT}
DEBUG?=		-g
