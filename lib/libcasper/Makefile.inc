# $FreeBSD$

.include <src.opts.mk>

.if ${MK_CASPER} != "no"
CFLAGS+=-DWITH_CASPER
.endif

.include "../Makefile.inc"
