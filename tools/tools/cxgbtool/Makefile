# $FreeBSD$

PROG=	cxgbtool
SRCS=	cxgbtool.c
MAN=
CFLAGS+= -I${.CURDIR}/../../../sys/dev/cxgb -I.
CFLAGS+= -DCONFIG_T3_REGS -DCHELSIO_INTERNAL
BINDIR?= /usr/sbin

.include <bsd.prog.mk>
