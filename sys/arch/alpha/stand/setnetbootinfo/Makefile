#	$OpenBSD: Makefile,v 1.3 2003/07/19 00:03:51 david Exp $
#	$NetBSD: Makefile,v 1.4 1997/04/06 08:41:36 cgd Exp $

.PATH:	${.CURDIR}/..

MAN=	setnetbootinfo.8
MANSUBDIR=alpha

.if ${MACHINE} == "alpha"
PROG=	setnetbootinfo
BINDIR=	/usr/mdec

CFLAGS+= -I${.CURDIR}/..
LDSTATIC=-static
.else
NOPROG=
.endif

.include <bsd.prog.mk>
