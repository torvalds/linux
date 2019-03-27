# $FreeBSD$

.PATH: ${SRCTOP}/contrib/tzcode/zic

PROG=	zdump
MAN=	zdump.8
SRCS=	zdump.c ialloc.c scheck.c

CFLAGS+= -DTM_GMTOFF=tm_gmtoff -DTM_ZONE=tm_zone -DSTD_INSPIRED -DPCTS
CFLAGS+= -DHAVE_LONG_DOUBLE -DTZDIR=\"${SHAREDIR}/zoneinfo\" -Demkdir=mkdir
CFLAGS+= -I${.CURDIR:H} -I${SRCTOP}/contrib/tzcode/stdtime

WARNS?=	2

.include <bsd.prog.mk>
