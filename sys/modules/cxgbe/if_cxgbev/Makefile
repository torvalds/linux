#
# $FreeBSD$
#

CXGBE=	${SRCTOP}/sys/dev/cxgbe
.PATH: ${CXGBE} ${CXGBE}/common

KMOD=	if_cxgbev
SRCS=	bus_if.h
SRCS+=	device_if.h
SRCS+=	opt_inet.h
SRCS+=	opt_inet6.h
SRCS+=	opt_ofed.h
SRCS+=	opt_rss.h
SRCS+=	pci_if.h pci_iov_if.h
SRCS+=	t4_vf.c
SRCS+=	t4vf_hw.c

CFLAGS+= -I${CXGBE}

.include <bsd.kmod.mk>
