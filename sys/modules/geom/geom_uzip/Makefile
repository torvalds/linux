# $FreeBSD$

.PATH: ${SRCTOP}/sys/geom/uzip ${SRCTOP}/sys/net

KMOD=	geom_uzip
SRCS=	g_uzip.c g_uzip_zlib.c g_uzip_lzma.c g_uzip_wrkthr.c
SRCS+=	g_uzip.h g_uzip_dapi.h g_uzip_lzma.h g_uzip_zlib.h g_uzip_softc.h \
    g_uzip_wrkthr.h
#CFLAGS=	-g -DINVARIANT_SUPPORT -DINVARIANTS

.PATH:	${SRCTOP}/sys/net

CFLAGS+= -I${SRCTOP}/sys/contrib/xz-embedded/freebsd \
	-I${SRCTOP}/sys/contrib/xz-embedded/linux/lib/xz/
SRCS+=	opt_geom.h

.include <bsd.kmod.mk>
