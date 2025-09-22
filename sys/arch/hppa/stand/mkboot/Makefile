#	$OpenBSD: Makefile,v 1.6 2003/07/19 00:03:52 david Exp $

MAN=	mkboot.8
MANSUBDIR=hppa

.if ${MACHINE} == "hppa"
PROG=	mkboot
CC=	${HOSTCC}
LDSTATIC=	-static
.else
NOPROG=
.endif

.include <bsd.prog.mk>
