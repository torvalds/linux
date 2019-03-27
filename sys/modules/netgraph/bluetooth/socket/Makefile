# $Id: Makefile,v 1.2 2003/01/12 23:37:31 max Exp $
# $FreeBSD$

.PATH: ${SRCTOP}/sys/netgraph/bluetooth/socket

CFLAGS+=	-I${SRCTOP}/sys/netgraph/bluetooth/include

KMOD=		ng_btsocket
SRCS=		ng_btsocket.c \
		ng_btsocket_hci_raw.c \
		ng_btsocket_l2cap_raw.c \
		ng_btsocket_l2cap.c \
		ng_btsocket_rfcomm.c \
		ng_btsocket_sco.c

.include <bsd.kmod.mk>
