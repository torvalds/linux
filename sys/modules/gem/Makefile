# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/gem

KMOD=	if_gem
SRCS=	bus_if.h device_if.h if_gem.c if_gem_pci.c ${if_gem_sbus} miibus_if.h
SRCS+=	${ofw_bus_if} pci_if.h

.if ${MACHINE} == "sparc64"
if_gem_sbus=	if_gem_sbus.c
.endif

.if ${MACHINE_CPUARCH} == "powerpc" || ${MACHINE_CPUARCH} == "sparc64"
ofw_bus_if=	ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
