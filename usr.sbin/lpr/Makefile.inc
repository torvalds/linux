# $FreeBSD$

.include <src.opts.mk>

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif

.include "../Makefile.inc"
