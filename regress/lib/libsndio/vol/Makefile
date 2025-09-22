#	$OpenBSD: Makefile,v 1.3 2011/04/12 07:12:59 ratchov Exp $
PROG= vol
LDADD= -lsndio
CFLAGS+= -I${.CURDIR}/..
SRCS = vol.c tools.c
REGRESS_SKIP=
.PATH: ${.CURDIR}/..
.include <bsd.regress.mk>
