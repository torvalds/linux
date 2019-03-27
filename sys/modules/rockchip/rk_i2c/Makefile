# $FreeBSD$

.PATH: ${SRCTOP}/sys/arm64/rockchip

KMOD=	rk_i2c
SRCS=	rk_i2c.c

SRCS+=	\
	bus_if.h \
	clknode_if.h \
	device_if.h \
	iicbus_if.h \
	ofw_bus_if.h \
	opt_platform.h \

.include <bsd.kmod.mk>
