# $FreeBSD$

TESTSRC=	${SRCTOP}/contrib/netbsd-tests/lib/libc/tls/${.CURDIR:T}

LIB=		h_tls_dlopen
SHLIB_NAME=	h_tls_dlopen.so
SRCS=		h_tls_dlopen.c

MAN=

LIBDIR=		${TESTSBASE}/lib/libc/tls
SHLIB_MAJOR=	1

.include "../../Makefile.netbsd-tests"

.include <bsd.lib.mk>
