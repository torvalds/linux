#	$OpenBSD: Makefile,v 1.6 2019/11/20 18:11:28 kn Exp $

.if ${MACHINE} == "sparc64"

.PATH:	${.CURDIR}/../ldomctl

PROG=	ldomd
SRCS=	ldomd.c ds.c mdesc.c util.c var-config.c
CFLAGS+=-Wall
CFLAGS+=-I${.CURDIR}/../ldomctl

.else

NOPROG=	yes

.endif

MAN=	ldomd.8
MANSUBDIR=sparc64

.include <bsd.prog.mk>
