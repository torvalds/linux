# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	openbsd
SRCS=	imsg-buffer.c \
	imsg.c \
	ohash.c
.if !defined(BOOTSTRAPPING)
# Skip getdtablecount.c when bootstrapping since it doesn't compile for Linux
# and is not used by any of the bootstrap tools
SRCS+=	getdtablecount.c
.endif

INTERNALLIB=

CFLAGS+= -I${.CURDIR}

WARNS=	3

.include <bsd.lib.mk>
