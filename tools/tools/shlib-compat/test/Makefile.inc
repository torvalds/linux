# $FreeBSD$

LIB=		test${TESTNUM}
SHLIB_MAJOR=	0

SRCS+=		test.c

WARNS?=		3

DEBUG_FLAGS?=	-g

VERSION_DEF=	${.CURDIR}/../Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

MK_DEBUG_FILES=	yes
