# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/bhnd/bcma

KMOD=	bcma_bhndb
SRCS=	bcma_bhndb.c

SRCS+=	bhnd_bus_if.h bhnd_erom_if.h
SRCS+=	bhndb_bus_if.h bhndb_if.h
SRCS+=	device_if.h bus_if.h

.include <bsd.kmod.mk>
