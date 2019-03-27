# $FreeBSD$
KMOD=	vsimple
SRCS=	vhba_simple.c vhba.c
CFLAGS += -I${.CURDIR}/.. -DVHBA_MOD=\"vsimple\"
VPATH=	${.CURDIR}/..

.include <bsd.kmod.mk>
