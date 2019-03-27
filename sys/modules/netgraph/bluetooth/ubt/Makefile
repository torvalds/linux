# $Id: Makefile,v 1.2 2003/03/22 23:44:34 max Exp $
# $FreeBSD$

.PATH: ${SRCTOP}/sys/netgraph/bluetooth/drivers/ubt

CFLAGS+=	-I${SRCTOP}/sys/netgraph/bluetooth/include \
		-I${SRCTOP}/sys/netgraph/bluetooth/drivers/ubt

KMOD=		ng_ubt
SRCS=		ng_ubt.c opt_bus.h opt_usb.h device_if.h bus_if.h \
		usb_if.h usbdevs.h

.include <bsd.kmod.mk>
