# $FreeBSD$

#CFLAGS+=	-DGLXIIC_DEBUG

.PATH:	${SRCTOP}/sys/dev/glxiic
KMOD=	glxiic
SRCS=	glxiic.c
SRCS+=	device_if.h bus_if.h pci_if.h iicbus_if.h

.include <bsd.kmod.mk>
