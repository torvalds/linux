#	$OpenBSD: Makefile,v 1.21 2016/09/04 15:43:51 tedu Exp $

PROG=	savecore

SRCS=	savecore.c zopen.c
LDADD=	-lkvm
DPADD=	${LIBKVM}
MAN=	savecore.8

.include <bsd.prog.mk>
