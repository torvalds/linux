#	$OpenBSD: Makefile.inc,v 1.2 2012/09/05 21:21:13 eric Exp $

NOMAN=	noman

SRCS+=		common.c
CFLAGS+=	-Wall -Wstrict-prototypes -Werror -Wundef -g
LDSTATIC=	${STATIC}
PROG:=		${PROG}${EXT}
