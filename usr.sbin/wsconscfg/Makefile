#	$OpenBSD: Makefile,v 1.38 2016/05/11 21:52:51 deraadt Exp $

.if ${MACHINE} != "octeon"

PROG=	wsconscfg
SRCS=	wsconscfg.c

.else

NOPROG=yes

.endif

MAN=	wsconscfg.8

.include <bsd.prog.mk>
