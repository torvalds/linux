#	$OpenBSD: Makefile,v 1.3 2016/03/30 06:38:43 jmc Exp $

PROG =	rpc.rwalld
SRCS =	rwalld.c
MAN =	rpc.rwalld.8

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

.include <bsd.prog.mk>
