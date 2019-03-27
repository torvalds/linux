# $FreeBSD$

.include <src.opts.mk>

.PATH: ${SRCTOP}/contrib/tzcode/zic

PROG=	zic
MAN=	zic.8
SRCS=	zic.c ialloc.c scheck.c

CFLAGS+= -DTM_GMTOFF=tm_gmtoff -DTM_ZONE=tm_zone -DSTD_INSPIRED -DPCTS
CFLAGS+= -DHAVE_LONG_DOUBLE -DTZDIR=\"${SHAREDIR}/zoneinfo\" -Demkdir=mkdir
CFLAGS+= -DHAVE_STRERROR -DHAVE_UNISTD_H
CFLAGS+= -I${.CURDIR:H} -I${SRCTOP}/contrib/tzcode/stdtime

WARNS?=	2

.if ${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} >= 50300
CWARNFLAGS+=	-Wno-error=strict-overflow
.endif

.include <bsd.prog.mk>
