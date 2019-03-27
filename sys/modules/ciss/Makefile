# $FreeBSD$

KMOD		=  ciss
.PATH:		${SRCTOP}/sys/dev/${KMOD}
SRCS		=  ciss.c
SRCS		+= opt_scsi.h opt_cam.h
SRCS		+= device_if.h bus_if.h pci_if.h

#CFLAGS		+=-DCISS_DEBUG

.include <bsd.kmod.mk>
