# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/acpi_support

KMOD=	acpi_asus_wmi
CFLAGS+=-I${SRCTOP}/sys/dev/acpi_support
SRCS=	acpi_asus_wmi.c opt_acpi.h acpi_if.h acpi_wmi_if.h device_if.h bus_if.h

.include <bsd.kmod.mk>
