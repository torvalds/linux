#	$OpenBSD: Makefile,v 1.8 2020/05/28 20:38:21 fcambus Exp $	

.if ${MACHINE} == "i386" || ${MACHINE} == "amd64" || \
    ${MACHINE} == "arm64" || ${MACHINE} == "armv7" || \
    ${MACHINE} == "alpha"

PROG=	wsmoused
SRCS=	wsmoused.c mouse_protocols.c

.else

NOPROG=yes

.endif

MAN=	wsmoused.8 

.include <bsd.prog.mk>
