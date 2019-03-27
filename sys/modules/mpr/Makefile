# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/mpr

KMOD=	mpr
SRCS=	mpr_pci.c mpr.c mpr_sas.c mpr_table.c mpr_user.c
SRCS+=	mpr_config.c mpr_mapping.c mpr_sas_lsi.c
SRCS+=	opt_cam.h
SRCS+=	device_if.h bus_if.h pci_if.h

#CFLAGS += -DMPR_DEBUG

.include <bsd.kmod.mk>

CWARNFLAGS.mpr_sas.c=		${NO_WUNNEEDED_INTERNAL_DECL}
# XXX Work around clang warning, until maintainer approves fix.
CWARNFLAGS.mpr_mapping.c=	${NO_WSOMETIMES_UNINITIALIZED}
