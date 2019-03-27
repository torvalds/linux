# $FreeBSD$

.include <src.opts.mk>

_spath=	${SRCTOP}/contrib/ofed/libibnetdisc
.PATH: ${_spath} ${_spath}/man

SHLIBDIR?=	/usr/lib
LIB_CXX=	ibnetdisc
SHLIB_MAJOR=	5
MK_PROFILE=	no

SRCS= \
chassis.c \
g_hash_table.cpp \
ibnetdisc.c \
ibnetdisc_cache.c \
query_smp.c

MAN= \
ibnd_debug.3 \
ibnd_destroy_fabric.3 \
ibnd_discover_fabric.3 \
ibnd_find_node_dr.3 \
ibnd_find_node_guid.3 \
ibnd_iter_nodes.3 \
ibnd_iter_nodes_type.3 \
ibnd_show_progress.3

LIBADD=		osmcomp ibmad ibumad
CFLAGS+=	-DHAVE_CONFIG_H=1
CFLAGS+=	-I${_spath}
CFLAGS+=	-I${SYSROOT:U${DESTDIR}}/${INCLUDEDIR}/infiniband
.if ${COMPILER_FEATURES:Mc++11}
CXXFLAGS+=	-std=c++11
.endif
VERSION_MAP=	${_spath}/libibnetdisc.map

.include <bsd.lib.mk>
