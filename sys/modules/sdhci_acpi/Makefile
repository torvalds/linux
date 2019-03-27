# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/sdhci

KMOD=	sdhci_acpi
SRCS=	sdhci_acpi.c sdhci.h sdhci_if.h
SRCS+=	acpi_if.h device_if.h bus_if.h pci_if.h mmcbr_if.h
SRCS+=	opt_acpi.h opt_mmccam.h

.include <bsd.kmod.mk>
