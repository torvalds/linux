# $FreeBSD$

CONFS=	apmd.conf
PROG=	apmd
MAN=	apmd.8
MANSUBDIR= /i386
SRCS=	apmd.c apmdlex.l apmdparse.y y.tab.h

PACKAGE=apm

WARNS?=	3

LIBADD=	l

CFLAGS+= -I${.CURDIR}

test:
	./apmd -d -f etc/apmd.conf -n

.include <bsd.prog.mk>
