
# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/puc

KMOD=	puc
SRCS=	puc.c puc_cfg.c puc_pci.c puc_pccard.c
SRCS+=	bus_if.h device_if.h serdev_if.c serdev_if.h \
	card_if.h pci_if.h

.include <bsd.kmod.mk>
