# $FreeBSD$

.PATH: ${SRCTOP}/sys/i386/bios

KMOD=	apm
SRCS=	apm.c apm.h
SRCS+=	device_if.h bus_if.h

EXPORT_SYMS=	apm_display	\
		apm_softc

.include <bsd.kmod.mk>
