# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/ral

KMOD=	if_ral
SRCS=	rt2560.c rt2661.c rt2860.c if_ral_pci.c
SRCS+=	device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
