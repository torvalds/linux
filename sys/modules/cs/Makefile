# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/cs

KMOD=	if_cs
SRCS=	if_cs.c if_cs_isa.c if_cs_pccard.c
SRCS+=	bus_if.h card_if.h device_if.h isa_if.h pccarddevs.h

.include <bsd.kmod.mk>
