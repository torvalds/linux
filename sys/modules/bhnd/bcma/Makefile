# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/bhnd/bcma

KMOD=	bcma
SRCS=	bcma.c bcma_subr.c bcma_erom.c

SRCS+=	device_if.h bus_if.h
SRCS+=	bhnd_bus_if.h bhnd_erom_if.h bhnd_pmu_if.h

.include <bsd.kmod.mk>
