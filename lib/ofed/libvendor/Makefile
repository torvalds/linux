# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/opensm/libvendor
.PATH: ${_spath}

SHLIBDIR?=	/usr/lib
LIB=		osmvendor
SHLIB_MAJOR=	4
MK_PROFILE=	no

SRCS= \
osm_vendor_ibumad.c \
osm_vendor_ibumad_sa.c

LIBADD=		ibumad pthread
CFLAGS+=	-I${_spath}/.. -DHAVE_CONFIG_H=1
CFLAGS+=	-I${SYSROOT:U${DESTDIR}}/${INCLUDEDIR}/infiniband
VERSION_MAP=	${_spath}/libosmvendor.map

.include <bsd.lib.mk>
