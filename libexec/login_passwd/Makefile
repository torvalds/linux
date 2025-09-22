#	$OpenBSD: Makefile,v 1.8 2019/12/21 18:56:27 millert Exp $

PROG=	login_passwd
MAN=	login_passwd.8
DPADD=	${LIBUTIL}
LDADD=	-lutil
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	auth
BINMODE=4555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
