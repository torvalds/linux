# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/vmware/vmci

KMOD=	vmci
SRCS=	vmci.c vmci_datagram.c vmci_doorbell.c vmci_driver.c vmci_event.c
SRCS+=	vmci_hashtable.c vmci_kernel_if.c vmci_qpair.c vmci_queue_pair.c
SRCS+=	vmci_resource.c
SRCS+=	device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
