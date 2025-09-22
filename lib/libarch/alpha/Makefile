# $FreeBSD: src/lib/libio/Makefile,v 1.2 1999/08/28 00:04:42 peter Exp $
# $OpenBSD: Makefile,v 1.9 2016/03/30 06:38:40 jmc Exp $

MAN=		inb.2
MANSUBDIR=alpha

.if ${MACHINE} == "alpha"
NOPIC=
SRCS=		io.c bwx.c
CFLAGS+= 	-Wall -Wno-uninitialized -mbwx
.include <bsd.lib.mk>
.else
NOPROG=
.include <bsd.prog.mk>
.endif

