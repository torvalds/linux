# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/powermac_nvram

KMOD=	powermac_nvram
SRCS=	powermac_nvram.c powermac_nvramvar.h
SRCS+=	bus_if.h device_if.h
SRCS+=	ofw_bus_if.h

.include <bsd.kmod.mk>
