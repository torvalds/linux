# $FreeBSD$

LIBUCL=	${SRCTOP}/contrib/libucl

PACKAGE=lib${LIB}
LIB=		ucl
PRIVATELIB=	true
SHLIB_MAJOR=	1
SRCS=		ucl_emitter_streamline.c \
		ucl_emitter_utils.c \
		ucl_emitter.c \
		ucl_hash.c \
		ucl_msgpack.c \
		ucl_parser.c  \
		ucl_schema.c \
		ucl_sexp.c \
		ucl_util.c

.PATH:		${LIBUCL}/src \
		${LIBUCL}/include

INCS=		ucl.h
LIBADD=		m

WARNS=		1
CFLAGS+=	-I${LIBUCL}/include \
		-I${LIBUCL}/src \
		-I${LIBUCL}/uthash \
		-I${LIBUCL}/klib

.include <bsd.lib.mk>
