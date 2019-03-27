# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	devstat
SHLIBDIR?= /lib
# Bump DEVSTAT_USER_API_VER in devstat.h every time this is incremented.
SHLIB_MAJOR= 7
SRCS=	devstat.c
INCS=	devstat.h

LIBADD=	kvm

MAN=	devstat.3

MLINKS+=devstat.3 devstat_getnumdevs.3 
MLINKS+=devstat.3 devstat_getgeneration.3 
MLINKS+=devstat.3 devstat_getversion.3
MLINKS+=devstat.3 devstat_checkversion.3 
MLINKS+=devstat.3 devstat_getdevs.3 
MLINKS+=devstat.3 devstat_selectdevs.3
MLINKS+=devstat.3 devstat_buildmatch.3 
MLINKS+=devstat.3 devstat_compute_statistics.3
MLINKS+=devstat.3 devstat_compute_etime.3
MLINKS+=devstat.3 getnumdevs.3 
MLINKS+=devstat.3 getgeneration.3 
MLINKS+=devstat.3 getversion.3
MLINKS+=devstat.3 checkversion.3 
MLINKS+=devstat.3 getdevs.3 
MLINKS+=devstat.3 selectdevs.3
MLINKS+=devstat.3 buildmatch.3 
MLINKS+=devstat.3 compute_stats.3
MLINKS+=devstat.3 compute_etime.3

CFLAGS+=-I${.CURDIR}

WARNS?=	3

.include <bsd.lib.mk>
