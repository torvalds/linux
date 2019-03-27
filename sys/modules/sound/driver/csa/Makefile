# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/sound/pci

KMOD=	snd_csa
SRCS=	device_if.h bus_if.h mpufoi_if.h pci_if.h
SRCS+=	csa.c csamidi.c csapcm.c

.include <bsd.kmod.mk>
