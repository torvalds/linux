#	$OpenBSD: Makefile,v 1.6 2016/07/30 03:25:48 guenther Exp $

NOMAN=
#MAN=		boot.8
.if ${MACHINE} == "loongson"
PROG=		boot
.include	"${.CURDIR}/Makefile.inc"
.else
NOPROG=
.endif
CFLAGS+=-fno-pie
AFLAGS+=-fno-pie
LDFLAGS+=-nopie -znorelro

.include <bsd.prog.mk>
