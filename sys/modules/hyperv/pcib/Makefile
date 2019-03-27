# $FreeBSD$

.PATH:  ${SRCTOP}/sys/dev/hyperv/pcib \
	${SRCTOP}/sys/dev/hyperv/vmbus

KMOD=	vmbus_pcib
SRCS=	vmbus_pcib.c
SRCS+=	bus_if.h device_if.h pci_if.h pcib_if.h vmbus_if.h

CFLAGS+= -I${SRCTOP}/sys/dev/hyperv/pcib

.include <bsd.kmod.mk>
