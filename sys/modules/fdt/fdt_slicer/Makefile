# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/fdt

KMOD=	fdt_slicer
SRCS=	fdt_slicer.c

# Generated files...
SRCS+=	\
	bus_if.h \
	device_if.h \
	ofw_bus_if.h \
	opt_platform.h \

.include <bsd.kmod.mk>
