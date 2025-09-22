#	$OpenBSD: Makefile,v 1.5 2017/04/16 10:16:35 jsg Exp $

.if ${MACHINE} == "sparc64"

PROG=	mksuncd
CFLAGS+=-Wall -Wstrict-prototypes -Wmissing-prototypes
CDIAGFLAGS=

.else
NOPROG=
.endif

MAN=	mksuncd.1
MANSUBDIR=sparc64

.include <bsd.prog.mk>
