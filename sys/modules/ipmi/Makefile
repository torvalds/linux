# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/ipmi

# XXX - ipmi_smbus and ipmi_ssif depend on smbus
# XXX - ipmi_acpi depends on acpi
KMOD=	ipmi
SRCS=	ipmi.c
SRCS+=	bus_if.h device_if.h
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SRCS+=	ipmi_kcs.c ipmi_smic.c ipmi_smbios.c ipmi_ssif.c
SRCS+=	ipmi_acpi.c ipmi_isa.c ipmi_pci.c ipmi_smbus.c
SRCS+=	opt_acpi.h
SRCS+=	acpi_if.h isa_if.h pci_if.h smbus_if.h

SUBDIR+= ipmi_linux

.elif ${MACHINE_ARCH} == "powerpc64"
SRCS+=	ipmi_opal.c ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
