# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/le

KMOD=	if_le
SRCS=	am7990.c am79900.c ${if_le_isa} ${if_le_lebuffer}
SRCS+=	${if_le_ledma} if_le_pci.c lance.c ${lebuffer_sbus}
SRCS+=	bus_if.h device_if.h ${isa_if} ${ofw_bus_if} pci_if.h

.if ${MACHINE_CPUARCH} == "i386"
if_le_isa=	if_le_isa.c
isa_if=		isa_if.h
.endif

.if ${MACHINE} == "sparc64"
if_le_lebuffer=	if_le_lebuffer.c
if_le_ledma=	if_le_ledma.c
lebuffer_sbus=	lebuffer_sbus.c
ofw_bus_if=	ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
