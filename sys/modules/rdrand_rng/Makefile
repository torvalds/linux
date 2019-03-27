# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/random

KMOD=	rdrand_rng
SRCS=	ivy.c
SRCS+=	bus_if.h device_if.h

CFLAGS+= -I${SRCTOP}/sys

.include <bsd.kmod.mk>
