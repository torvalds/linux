# $FreeBSD$

PACKAGE=runtime
PROG=	nvmecontrol
SRCS=	nvmecontrol.c devlist.c firmware.c format.c identify.c identify_ext.c logpage.c \
	perftest.c reset.c ns.c nvme_util.c power.c nc_util.c
MAN=	nvmecontrol.8
LDFLAGS+= -rdynamic
SUBDIR=	modules

.PATH:	${SRCTOP}/sys/dev/nvme

.include <bsd.prog.mk>
.include <bsd.subdir.mk>
