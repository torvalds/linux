# $FreeBSD$

LIBXOSRC=	${SRCTOP}/contrib/libxo

.PATH: ${LIBXOSRC}/encoder/test

SHLIB_NAME=	libenc_test.so

LIBDIR=		${TESTSBASE}/lib/libxo

SYMLINKS+=	${LIBDIR}/${SHLIB_NAME} /usr/lib/libxo/encoder/test.enc

SRCS=		enc_test.c

CFLAGS+=	-I${LIBXOSRC}/libxo

LIBADD=		xo

.include <bsd.lib.mk>
