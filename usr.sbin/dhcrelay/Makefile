# $OpenBSD: Makefile,v 1.4 2017/04/05 14:43:14 reyk Exp $

.include <bsd.own.mk>

SRCS=	dhcrelay.c dispatch.c log.c bpf.c packet.c
PROG=	dhcrelay
MAN=	dhcrelay.8

CFLAGS+=-Wall
CFLAGS+=-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=-Wmissing-declarations
CFLAGS+=-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=-Wsign-compare

.include <bsd.prog.mk>
