# $FreeBSD$

.PATH: ${SRCTOP}/sys/arm/broadcom/bcm2835/

KMOD=	bcm283x_clkman
SRCS=	bcm2835_clkman.c

EXPORT_SYMS=	bcm2835_clkman_set_frequency

SRCS+= bus_if.h device_if.h ofw_bus_if.h

.include <bsd.kmod.mk>
