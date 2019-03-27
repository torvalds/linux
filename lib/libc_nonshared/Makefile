# $FreeBSD$

# We're actually creating a libc_noshared.a that is PIC along side libc.so.*
# It is used exclusively with libc.so.* - there is no need for any other
# compile modes.
# bsd.lib.mk doesn't have an easy way to express that.
MK_PROFILE?=no
.include <src.opts.mk>
NO_PIC=
# -fpic on some platforms, -fPIC on others.
CFLAGS+=${PICFLAG} -DPIC -fvisibility=hidden

LIB=	c_nonshared

LIBC_NONSHARED_SRCS=

# So that an empty .a file doesn't cause errors.
SRCS=	__stub.c

.if ${MK_ICONV} == "yes"
.PATH: ${SRCTOP}/lib/libc/iconv
.include "Makefile.iconv"
CFLAGS+=-I${SRCTOP}/lib/libc/iconv
.endif

SRCS+=	${LIBC_NONSHARED_SRCS}

.include <bsd.lib.mk>
