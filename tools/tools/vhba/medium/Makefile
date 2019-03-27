# $FreeBSD$
KMOD=	vmedium
SRCS=	vhba_medium.c vhba.c
CFLAGS += -I${.CURDIR}/.. -DVHBA_MOD=\"vmedium\"
VPATH=	${.CURDIR}/..

.include <bsd.kmod.mk>
