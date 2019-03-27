# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/libibcm
.PATH: ${_spath}

SHLIBDIR?=	/usr/lib
LIB=		ibcm
SHLIB_MAJOR=	1
MK_PROFILE=	no
SRCS=		cm.c
LIBADD=		ibverbs
CFLAGS+=	-I${_spath}
MAN=
VERSION_MAP=	${_spath}/libibcm.map

.include <bsd.lib.mk>
