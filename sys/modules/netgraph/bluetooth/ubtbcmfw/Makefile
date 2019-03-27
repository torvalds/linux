# $Id: Makefile,v 1.1 2003/04/27 00:22:12 max Exp $
# $FreeBSD$

.PATH: ${SRCTOP}/sys/netgraph/bluetooth/drivers/ubtbcmfw

CFLAGS+=	-I${SRCTOP}/sys/netgraph/bluetooth/include \
		-I${SRCTOP}/sys/netgraph/bluetooth/drivers/ubtbcmfw

KMOD=		ubtbcmfw
SRCS=		ubtbcmfw.c opt_bus.h opt_usb.h device_if.h bus_if.h \
		usb_if.h vnode_if.h usbdevs.h

.include <bsd.kmod.mk>
