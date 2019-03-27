# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	geom
SHLIBDIR?= /lib
SRCS+=	geom_getxml.c
SRCS+=	geom_stats.c
SRCS+=	geom_xml2tree.c
SRCS+=	geom_ctl.c
SRCS+=	geom_util.c
INCS=	libgeom.h

CFLAGS += -I${.CURDIR}

WARNS?=	3

LIBADD+=	bsdxml sbuf

MAN=	libgeom.3

MLINKS+= \
	libgeom.3 geom_stats_open.3 \
	libgeom.3 geom_stats_close.3 \
	libgeom.3 geom_stats_resync.3 \
	libgeom.3 geom_stats_snapshot_get.3 \
	libgeom.3 geom_stats_snapshot_free.3 \
	libgeom.3 geom_stats_snapshot_timestamp.3 \
	libgeom.3 geom_stats_snapshot_reset.3 \
	libgeom.3 geom_stats_snapshot_next.3 \
	libgeom.3 gctl_get_handle.3 \
	libgeom.3 gctl_ro_param.3 \
	libgeom.3 gctl_rw_param.3 \
	libgeom.3 gctl_issue.3 \
	libgeom.3 gctl_free.3 \
	libgeom.3 gctl_dump.3 \
	libgeom.3 geom_getxml.3 \
	libgeom.3 geom_xml2tree.3 \
	libgeom.3 geom_gettree.3 \
	libgeom.3 geom_deletetree.3 \
	libgeom.3 g_close.3 \
	libgeom.3 g_delete.3 \
	libgeom.3 g_device_path.3 \
	libgeom.3 g_flush.3 \
	libgeom.3 g_get_ident.3 \
	libgeom.3 g_get_name.3 \
	libgeom.3 g_mediasize.3 \
	libgeom.3 g_open.3 \
	libgeom.3 g_open_by_ident.3 \
	libgeom.3 g_providername.3 \
	libgeom.3 g_sectorsize.3

.include <bsd.lib.mk>
