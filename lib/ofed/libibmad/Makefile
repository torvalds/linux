# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/libibmad
.PATH: ${_spath}

SHLIBDIR?=	/usr/lib
LIB=		ibmad
SHLIB_MAJOR=	5
MK_PROFILE=	no

SRCS= \
bm.c \
cc.c \
dump.c \
fields.c \
gs.c \
mad.c \
portid.c \
register.c \
resolve.c \
rpc.c \
sa.c \
serv.c \
smp.c \
vendor.c

LIBADD=		ibumad
CFLAGS+=	-DHAVE_CONFIG_H=1
CFLAGS+=	-I${_spath}
VERSION_MAP=	${_spath}/libibmad.map

.include <bsd.lib.mk>

