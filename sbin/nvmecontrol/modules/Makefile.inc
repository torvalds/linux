# $FreeBSD$

PACKAGE=runtime
NVMECONTROLDIR=	${SRCTOP}/sbin/nvmecontrol

MK_INSTALLLIB=	no
MK_PROFILE=	no

CFLAGS+=	-I${NVMECONTROLDIR}

SHLIB_NAME?=	${LIB}.so
LIBDIR=		/lib/nvmecontrol
