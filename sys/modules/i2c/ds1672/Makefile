# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/iicbus
KMOD	= ds1672
SRCS	= ds1672.c 

# Generated files...
SRCS+=	\
	bus_if.h \
	clock_if.h \
	device_if.h \
	iicbus_if.h \
	opt_platform.h \

.if !empty(OPT_FDT)
SRCS+=	ofw_bus_if.h
.endif

.include <bsd.kmod.mk>
