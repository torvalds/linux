# $FreeBSD$

CXGBE = ${SRCTOP}/sys/dev/cxgbe
.PATH: ${CXGBE}/cxgbei

KMOD=	cxgbei

SRCS=	cxgbei.c
SRCS+=	icl_cxgbei.c
SRCS+=	bus_if.h
SRCS+=	device_if.h
SRCS+=	opt_inet.h
SRCS+=	opt_inet6.h
SRCS+=	pci_if.h
SRCS+=	opt_cam.h
SRCS+=	icl_conn_if.h

CFLAGS+= -I${CXGBE}

MFILES=	kern/bus_if.m kern/device_if.m dev/iscsi/icl_conn_if.m dev/pci/pci_if.m

.include <bsd.kmod.mk>
