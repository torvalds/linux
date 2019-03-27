# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/aacraid

.if ${MACHINE_CPUARCH} == "i386"
SUBDIR= aacraid_linux
.endif

KMOD=	aacraid
SRCS=	aacraid.c aacraid_pci.c aacraid_cam.c
SRCS+=	opt_scsi.h opt_cam.h opt_aacraid.h
SRCS+=	device_if.h bus_if.h pci_if.h

# To enable debug output from the driver, uncomment these two lines.
#CFLAGS+= -DAACRAID_DEBUG=2
#SRCS+=	aacraid_debug.c

.include <bsd.kmod.mk>
