# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/ow

KMOD=	owc
SRCS=	owc_gpiobus.c
SRCS+=	gpio_if.h gpiobus_if.h owll_if.h ofw_bus_if.h bus_if.h device_if.h
SRCS+=	opt_platform.h

.include <bsd.kmod.mk>
