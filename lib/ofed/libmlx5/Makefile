# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/libmlx5
.PATH: ${_spath}

SHLIBDIR?=	/lib
LIB=		mlx5
SHLIB_MAJOR=	1
MK_PROFILE=	no

SRCS= \
buf.c \
cq.c \
dbrec.c \
mlx5.c \
qp.c \
srq.c \
verbs.c

LIBADD=		ibverbs pthread
CFLAGS+=	-I${_spath}
VERSION_MAP=	${_spath}/libmlx5.map

.include <bsd.lib.mk>

