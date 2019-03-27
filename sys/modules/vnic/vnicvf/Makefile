# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

S=	${SRCTOP}/sys

.PATH: $S/dev/vnic

KMOD=	vnicvf
SRCS=	nicvf_main.c nicvf_queues.c
SRCS+=	opt_platform.h ofw_bus_if.h lmac_if.h miibus_if.h pci_if.h bus_if.h device_if.h opt_inet.h opt_inet6.h

CFLAGS+=	-DFDT -DPCI_IOV

.include <bsd.kmod.mk>
