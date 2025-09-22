#	$OpenBSD: Makefile,v 1.8 2003/07/19 00:03:51 david Exp $
#	$NetBSD: Makefile,v 1.2 1996/11/06 23:09:13 cgd Exp $

.PATH:	${.CURDIR}/..

MAN=	installboot.8
MANSUBDIR=alpha

.if ${MACHINE} == "alpha"
PROG=	installboot
BINDIR=	/usr/mdec
DPADD+= ${LIBUTIL}
LDADD+= -lutil
LDSTATIC=-static
.else
NOPROG=
.endif

.include <bsd.prog.mk>
