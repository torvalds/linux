# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/acpi_support

KMOD=	aibs
SRCS=	atk0110.c
SRCS+=	opt_acpi.h acpi_if.h bus_if.h device_if.h
SRCS+=	opt_ddb.h

.include <bsd.kmod.mk>
