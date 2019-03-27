# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/nvdimm

KMOD=	nvdimm
SRCS=	nvdimm.c \
	nvdimm_nfit.c \
	nvdimm_ns.c \
	nvdimm_spa.c

SRCS+=	acpi_if.h bus_if.h device_if.h

.include <bsd.kmod.mk>
