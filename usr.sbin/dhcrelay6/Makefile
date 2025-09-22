# $OpenBSD: Makefile,v 1.2 2017/04/16 00:52:54 aoyama Exp $

.include <bsd.own.mk>

SRCS=	dhcrelay6.c dispatch.c log.c bpf.c packet.c
PROG=	dhcrelay6
MAN=	dhcrelay6.8

CFLAGS+=-Wall -Wextra
CFLAGS+=-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations
CFLAGS+=-Wshadow -Wpointer-arith -Wsign-compare

.include <bsd.prog.mk>
