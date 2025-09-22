#	$OpenBSD: Makefile,v 1.8 2016/09/03 13:37:41 guenther Exp $

MAN+=	arm_sync_icache.2 arm_drain_writebuf.2
MANSUBDIR=armv7

.if ${MACHINE_ARCH} == "arm"
NOPIC=
SRCS+=	arm_sync_icache.c arm_drain_writebuf.c
.include <bsd.lib.mk>
.else
NOPROG=
.include <bsd.prog.mk>
.endif
