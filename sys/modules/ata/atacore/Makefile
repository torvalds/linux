# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/ata

KMOD=	ata
SRCS=	ata-all.c ata_if.c ata-lowlevel.c
SRCS+=	ata_if.h bus_if.h device_if.h opt_cam.h pci_if.h

.include <bsd.kmod.mk>
