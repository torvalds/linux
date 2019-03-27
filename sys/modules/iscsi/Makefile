# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

.PATH:	${SRCTOP}/sys/dev/iscsi/
KMOD=	iscsi

SRCS=	iscsi.c
SRCS+=	icl.c
SRCS+=	icl_soft.c
SRCS+=	icl_soft_proxy.c
SRCS+=	opt_cam.h
SRCS+=	bus_if.h
SRCS+=	device_if.h
SRCS+=	icl_conn_if.c
SRCS+=	icl_conn_if.h

.if ${MK_OFED} != "no" || defined(ALL_MODULES)
CFLAGS+=-DICL_KERNEL_PROXY
.endif

MFILES=	kern/bus_if.m kern/device_if.m dev/iscsi/icl_conn_if.m

.include <bsd.kmod.mk>
