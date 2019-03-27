# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/usb/wlan

KMOD    = if_rsu
SRCS    = if_rsu.c if_rsureg.h \
	  bus_if.h device_if.h \
	  opt_bus.h opt_usb.h opt_wlan.h usb_if.h usbdevs.h

.include <bsd.kmod.mk>
