#
# $FreeBSD$
#

PACKAGE=lib${LIB}
SHLIBDIR?=	/lib

.include <bsd.own.mk>

LIB=		pjdlog
SRCS=		pjdlog.c

SHLIB_MAJOR=	0

CFLAGS+=-I${.CURDIR}

LIBADD=	util

.include <bsd.lib.mk>
