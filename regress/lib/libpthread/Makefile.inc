# $OpenBSD: Makefile.inc,v 1.11 2019/05/14 16:43:26 bluhm Exp $
# Copyright (c) 1993 Chris Provenzano, proven@athena.mit.edu

LIBPTHREAD=	/usr/lib/libpthread.a

LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}

CFLAGS+=	-Wall # -Werror
#DEBUG=		-ggdb
CFLAGS+=	-DSRCDIR='"${.CURDIR}"'
CFLAGS+=	-I${.CURDIR}/../include
