# $FreeBSD$

PROG=	mwlstats
BINDIR=	/usr/local/bin
MAN=

SRCS=	main.c mwlstats.c
LIBADD=	bsdstat
CFLAGS+=-I${.CURDIR}/../../../../lib/libbsdstat/

.include <bsd.prog.mk>

CFLAGS+= -I.
