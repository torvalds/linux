# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/de

KMOD=	if_de
SRCS=	if_de.c opt_ddb.h device_if.h bus_if.h pci_if.h

# Compile-time options.

#  Enable debugging:
#CFLAGS+=	-DTULIP_DEBUG

#  Enable performance statistics:
#CFLAGS+=	-DTULIP_PERFSTATS

.include <bsd.kmod.mk>
