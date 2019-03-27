# $FreeBSD$

S=     ${SRCTOP}/sys

.PATH: $S/dev/usb/net

KMOD=	if_muge
SRCS=	opt_bus.h opt_usb.h device_if.h bus_if.h usb_if.h usbdevs.h \
		miibus_if.h miidevs.h opt_inet.h opt_platform.h ofw_bus_if.h \
		if_muge.c
SRCS+=

.include <bsd.kmod.mk>
