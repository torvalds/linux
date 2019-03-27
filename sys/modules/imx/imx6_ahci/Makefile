# $FreeBSD$

.PATH: ${SRCTOP}/sys/arm/freescale/imx

KMOD=	imx6_ahci
SRCS=	imx6_ahci.c

# Generated files...
SRCS+=	\
	bus_if.h \
	device_if.h \
	ofw_bus_if.h \

.include <bsd.kmod.mk>
