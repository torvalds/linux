# $FreeBSD$

.include <bsd.init.mk>

LIB=		firewire

.PATH:		${SYSDIR}/dev/dcons ${SYSDIR}/dev/firewire
SRCS+=		firewire.c fwohci.c dconsole.c
SRCS+=		dcons.c fwcrom.c

CFLAGS+=	-D_BOOT

CFLAGS+=	-I${LDRSRC}
CFLAGS+=	-I${BOOTSRC}/i386/libi386

CFLAGS+=	-Wformat -Wall

.include <bsd.lib.mk>
