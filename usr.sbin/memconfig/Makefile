# $FreeBSD: src/usr.sbin/memcontrol/Makefile,v 1.2 1999/09/29 21:43:31 chris Exp $

.if ${MACHINE} == "i386" || ${MACHINE} == "amd64"
PROG=		memconfig
.else
NOPROG=yes
.endif

MAN=		memconfig.8
MANSUBDIR=	i386 amd64

.include <bsd.prog.mk>
