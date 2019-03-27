# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/otus

KMOD    = if_otus
SRCS    = if_otus.c if_otusreg.h	\
	  bus_if.h device_if.h		\
	  opt_bus.h opt_usb.h opt_wlan.h usb_if.h usbdevs.h

.include <bsd.kmod.mk>
