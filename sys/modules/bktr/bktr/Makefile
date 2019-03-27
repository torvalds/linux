# $FreeBSD$

# Makefile for the bktr Brooktree Bt848/Bt878 PCI video capture device

.PATH: ${SRCTOP}/sys/dev/bktr

KMOD=	bktr 
SRCS=	bktr_core.c bktr_os.c bktr_audio.c bktr_tuner.c bktr_card.c msp34xx.c \
	opt_bktr.h bus_if.h device_if.h \
	pci_if.h

CFLAGS+= -DBKTR_FREEBSD_MODULE

.if defined(BKTR_USE_FREEBSD_SMBUS)
SRCS+=	bktr_i2c.c iicbb_if.h iicbus_if.h smbus_if.h
CFLAGS+= -DBKTR_USE_FREEBSD_SMBUS
.endif

.include <bsd.kmod.mk>
