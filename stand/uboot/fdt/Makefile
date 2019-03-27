# $FreeBSD$

.include <bsd.init.mk>

.PATH: ${LDRSRC}

LIB=		uboot_fdt
WARNS?=		2

SRCS=		uboot_fdt.c

# U-Boot library headers
CFLAGS+=	-I${UBOOTSRC}/lib

# libfdt headers
CFLAGS+=	-I${FDTSRC}

# Pick up the bootstrap header for some interface items
CFLAGS+=	-I${LDRSRC}

.include <bsd.lib.mk>
