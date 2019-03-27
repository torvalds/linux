# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/fe

KMOD=	if_fe
SRCS=	if_fe.c if_fe_isa.c if_fe_pccard.c

SRCS+=	bus_if.h card_if.h device_if.h isa_if.h pccarddevs.h

.include <bsd.kmod.mk>
