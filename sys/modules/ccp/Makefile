# $FreeBSD$

.PATH: ${SRCTOP}/sys/crypto/ccp

KMOD=	ccp

SRCS=	ccp.c ccp_hardware.c ccp_lsb.c
SRCS+=	ccp.h ccp_hardware.h ccp_lsb.h
SRCS+=	opt_ddb.h
SRCS+=	bus_if.h
SRCS+=	device_if.h
SRCS+=	cryptodev_if.h
SRCS+=	pci_if.h

CFLAGS+=	-fms-extensions
CFLAGS.clang+=	-Wno-microsoft-anon-tag

MFILES=	kern/bus_if.m kern/device_if.m opencrypto/cryptodev_if.m \
	dev/pci/pci_if.m

.include <bsd.kmod.mk>
