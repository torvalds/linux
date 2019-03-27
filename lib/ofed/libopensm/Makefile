# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/opensm/opensm
.PATH:	${_spath}

SHLIBDIR?=	/usr/lib
LIB=		opensm
SHLIB_MAJOR=	5
MK_PROFILE=	no

SRCS= \
osm_log.c \
osm_mad_pool.c \
osm_helper.c

LIBADD=		pthread
CFLAGS+=	-I${_spath} -I${_spath}/.. -DHAVE_CONFIG_H=1
CFLAGS+=	-I${SYSROOT:U${DESTDIR}}/${INCLUDEDIR}/infiniband
VERSION_MAP=	${_spath}/libopensm.map

.include <bsd.lib.mk>
