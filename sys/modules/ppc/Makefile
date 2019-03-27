# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/ppc
KMOD=	ppc
SRCS=	bus_if.h device_if.h ppbus_if.h isa_if.h pci_if.h serdev_if.h \
	opt_ppc.h opt_isa.h \
	ppc.c ppc_pci.c ppc_puc.c

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	ppc_acpi.c ppc_isa.c
.endif

.include <bsd.kmod.mk>
