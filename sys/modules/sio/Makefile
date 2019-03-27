# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/sio

KMOD=	sio
SRCS=	bus_if.h card_if.h device_if.h isa_if.h pci_if.h serdev_if.h \
	opt_gdb.h opt_kdb.h opt_sio.h \
	sio.c sio_isa.c sio_pccard.c sio_pci.c sio_puc.c pccarddevs.h

.include <bsd.kmod.mk>
