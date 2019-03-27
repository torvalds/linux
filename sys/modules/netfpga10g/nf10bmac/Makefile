# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/netfpga10g/nf10bmac

KMOD=	if_nf10bmac
SRCS=	if_nf10bmac.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_device_polling.h
SRCS+=	opt_netfpga.h

.if ${MACHINE_CPUARCH} == "mips"
SRCS+=	if_nf10bmac_fdt.c ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
