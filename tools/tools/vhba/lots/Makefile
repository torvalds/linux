# $FreeBSD$
KMOD=	vlots
SRCS=	vhba_lots.c vhba.c
CFLAGS += -I${.CURDIR}/.. -DVHBA_MOD=\"vlots\"
VPATH=	${.CURDIR}/..

.include <bsd.kmod.mk>
