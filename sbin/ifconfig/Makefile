#	$OpenBSD: Makefile,v 1.17 2020/06/22 02:08:43 dlg Exp $

PROG=	ifconfig
SRCS=	ifconfig.c brconfig.c sff.c
MAN=	ifconfig.8

LDADD=	-lutil -lm
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>
