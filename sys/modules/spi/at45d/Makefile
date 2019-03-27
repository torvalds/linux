# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/flash

KMOD=	at45d
SRCS=	at45d.c

# Generated files...
SRCS+=	\
	bus_if.h \
	device_if.h \
	opt_platform.h \
	spibus_if.h \

.if !empty(OPT_FDT)
SRCS+=	ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
