# $FreeBSD$

PROG=	colldef
SRCS=	parse.y scan.l y.tab.h
LFLAGS=	-8 -i
CFLAGS+=-I. -I${.CURDIR}
CFLAGS+=-DCOLLATE_DEBUG -DYY_NO_UNPUT -DYY_NO_INPUT
LIBADD=	l

WARNS?=	2

.include <bsd.prog.mk>
