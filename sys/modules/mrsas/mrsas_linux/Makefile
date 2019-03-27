# Makefile for mrsas driver
# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/mrsas

KMOD=	mrsas_linux
SRCS=	mrsas_linux.c
SRCS+=	device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
