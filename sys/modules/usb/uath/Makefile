# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/usb/wlan

KMOD    = if_uath
SRCS    = if_uath.c if_uathvar.h if_uathreg.h	\
	  bus_if.h device_if.h \
	  opt_bus.h opt_usb.h opt_wlan.h usb_if.h usbdevs.h

.include <bsd.kmod.mk>
