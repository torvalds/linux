# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/random

KMOD=	padlock_rng
SRCS=	nehemiah.c
SRCS+=	bus_if.h device_if.h

CFLAGS+= -I${SRCTOP}/sys

.include <bsd.kmod.mk>
