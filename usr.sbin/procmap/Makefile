#	$OpenBSD: Makefile,v 1.7 2017/06/19 03:06:26 dlg Exp $

PROG=		procmap
SRCS=		procmap.c
CFLAGS+=	-Wall
LDADD=		-lkvm
DPADD=		${LIBKVM}

.include <bsd.prog.mk>
