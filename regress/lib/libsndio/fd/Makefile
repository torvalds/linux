#	$OpenBSD: Makefile,v 1.3 2011/04/12 07:12:59 ratchov Exp $
PROG= fd
LDADD= -lsndio
CFLAGS+= -I${.CURDIR}/..
SRCS = fd.c tools.c
REGRESS_SKIP=
.PATH: ${.CURDIR}/..
.include <bsd.regress.mk>
