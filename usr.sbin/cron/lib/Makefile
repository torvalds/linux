# $FreeBSD$

LIB=	cron
INTERNALLIB=
SRCS= entry.c env.c misc.c

WARNS?=	3

CFLAGS+= -I${.CURDIR:H}/cron
CFLAGS+= -DLOGIN_CAP -DPAM

.include <bsd.lib.mk>
