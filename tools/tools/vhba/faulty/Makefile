# $FreeBSD$
KMOD=	vfaulty
SRCS=	vhba_faulty.c vhba.c
CFLAGS += -I${.CURDIR}/.. -DVHBA_MOD=\"vfaulty\"
VPATH=	${.CURDIR}/..

.include <bsd.kmod.mk>
