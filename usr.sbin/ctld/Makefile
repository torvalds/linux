# $FreeBSD$

.include <src.opts.mk>

CFLAGS+=-I${SRCTOP}/contrib/libucl/include
.PATH:  ${SRCTOP}/contrib/libucl/include

PROG=		ctld
SRCS=		chap.c ctld.c discovery.c isns.c kernel.c keys.c log.c
SRCS+=		login.c parse.y pdu.c token.l y.tab.h uclparse.c
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${SRCTOP}/sys
CFLAGS+=	-I${SRCTOP}/sys/cam/ctl
CFLAGS+=	-I${SRCTOP}/sys/dev/iscsi
#CFLAGS+=	-DICL_KERNEL_PROXY
MAN=		ctld.8 ctl.conf.5

LIBADD=		bsdxml l md sbuf util ucl m nv

YFLAGS+=	-v
CLEANFILES=	y.tab.c y.tab.h y.output

WARNS?=		6
NO_WMISSING_VARIABLE_DECLARATIONS=

.if ${MK_ISCSI} != "no"
CFLAGS+=	-DWANT_ISCSI
.endif

.include <bsd.prog.mk>
