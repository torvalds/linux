# $FreeBSD$

PACKAGE=iscsi_legacy
SRCS= iscontrol.c pdu.c fsm.c config.c login.c auth_subr.c misc.c
PROG= iscontrol
LIBADD=	cam md
S= ${SRCTOP}/sys

WARNS?=	3
CFLAGS+=	-I$S

MAN= iscontrol.8

.include <bsd.prog.mk>
