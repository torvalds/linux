# $FreeBSD$

.PATH: ${SRCTOP}/sys/arm/broadcom/bcm2835/

KMOD=	rpi_ft5406
SRCS=	bcm2835_ft5406.c

SRCS+= bus_if.h device_if.h mbox_if.h ofw_bus_if.h

.include <bsd.kmod.mk>
