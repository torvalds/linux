# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/mps

KMOD=	mps
SRCS=	mps_pci.c mps.c mps_sas.c mps_table.c mps_user.c
SRCS+=	mps_config.c mps_mapping.c mps_sas_lsi.c
SRCS+=	opt_cam.h
SRCS+=	device_if.h bus_if.h pci_if.h

#CFLAGS += -DMPS_DEBUG

.include <bsd.kmod.mk>

CWARNFLAGS.mps_sas.c=		${NO_WUNNEEDED_INTERNAL_DECL}
# XXX Work around clang warning, until maintainer approves fix.
CWARNFLAGS.mps_mapping.c=	${NO_WSOMETIMES_UNINITIALIZED}
