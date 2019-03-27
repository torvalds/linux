# $FreeBSD$

.PATH: ${SRCTOP}/sys/cam/ctl

KMOD=	cfiscsi

#CFLAGS+=-DICL_KERNEL_PROXY

MFILES=	dev/iscsi/icl_conn_if.m
SRCS=	ctl_frontend_iscsi.c icl_conn_if.h

.include <bsd.kmod.mk>
