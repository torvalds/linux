# $FreeBSD$
KMOD=	vrptluns
SRCS=	vhba_rptluns.c vhba.c
CFLAGS += -I${.CURDIR}/.. -DVHBA_MOD=\"vrptluns\"
VPATH=	${.CURDIR}/..

.include <bsd.kmod.mk>
