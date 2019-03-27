# $Id: Makefile,v 1.1 2002/11/24 20:40:04 max Exp $
# $FreeBSD$

.PATH: ${SRCTOP}/sys/netgraph/bluetooth/drivers/h4

CFLAGS+=	-I${SRCTOP}/sys/netgraph/bluetooth/include \
		-I${SRCTOP}/sys/netgraph/bluetooth/drivers/h4

KMOD=		ng_h4
SRCS=		ng_h4.c

.include <bsd.kmod.mk>
