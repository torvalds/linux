# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/bhnd/siba

KMOD=	siba
SRCS=	siba.c siba_subr.c \
	siba_erom.c

SRCS+=	device_if.h bus_if.h
SRCS+=	bhnd_bus_if.h bhnd_chipc_if.h \
	bhnd_erom_if.h bhnd_pmu_if.h \
	bhnd_pwrctl_if.h

.include <bsd.kmod.mk>
