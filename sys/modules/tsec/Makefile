# $FreeBSD$

SYSDIR?=${SRCTOP}/sys

.PATH:	${SYSDIR}/dev/tsec

KMOD=	if_tsec
SRCS=	if_tsec.c if_tsec_fdt.c
SRCS+=	bus_if.h device_if.h miibus_if.h miidevs.h ofw_bus_if.h

.include <bsd.kmod.mk>
