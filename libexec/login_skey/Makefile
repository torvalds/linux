#	$OpenBSD: Makefile,v 1.2 2002/05/16 03:51:23 millert Exp $

PROG=	login_skey
MAN=	login_skey.8
DPADD=	${LIBSKEY}
LDADD=	-lskey
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	auth
BINMODE=2555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
