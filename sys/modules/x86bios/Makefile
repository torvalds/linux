# $FreeBSD$

KMOD=	x86bios
SRCS=	opt_x86bios.h
SRCS+=	bus_if.h device_if.h pci_if.h

.PATH:	${SRCTOP}/sys/contrib/x86emu
.PATH:	${SRCTOP}/sys/compat/x86bios
SRCS+=	x86bios.c
.if ${MACHINE_ARCH} != "i386"
SRCS+=	x86emu.c
.endif

.include <bsd.kmod.mk>
