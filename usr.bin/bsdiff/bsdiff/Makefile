# $FreeBSD$

PROG=		bsdiff

# libdivsufsort configured with:
# cmake -DCMAKE_BUILD_TYPE="Release" -DBUILD_DIVSUFSORT64=ON
.PATH:		${SRCTOP}/contrib/libdivsufsort/lib
CFLAGS+=	-DHAVE_CONFIG_H=1 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CFLAGS+=	-D_LARGE_FILES -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS
CFLAGS+=	-D__STDC_LIMIT_MACROS -DBUILD_DIVSUFSORT64
CFLAGS+=	-I${SRCTOP}/contrib/libdivsufsort/include -I${.CURDIR}
SRCS=		divsufsort.c sssort.c trsort.c utils.c

SRCS+=		bsdiff.c

LIBADD=	bz2

.include <bsd.prog.mk>
