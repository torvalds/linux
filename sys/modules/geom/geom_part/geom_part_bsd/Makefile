# $FreeBSD$

.PATH: ${SRCTOP}/sys/geom/part ${SRCTOP}/sys/geom

KMOD=	geom_part_bsd
SRCS=	g_part_bsd.c geom_bsd_enc.c

SRCS+=	bus_if.h device_if.h g_part_if.h

.include <bsd.kmod.mk>
