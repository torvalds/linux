#	$OpenBSD: Makefile,v 1.4 2016/03/30 06:38:43 jmc Exp $
#	$NetBSD: Makefile,v 1.2 1997/06/12 16:18:50 mrg Exp $

PROG=	swapctl
SRCS=	swapctl.c swaplist.c
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	swapctl.8

LINKS=	${BINDIR}/swapctl ${BINDIR}/swapon

.include <bsd.prog.mk>
