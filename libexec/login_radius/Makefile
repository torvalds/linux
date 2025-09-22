#	$OpenBSD: Makefile,v 1.3 2024/07/17 20:50:28 yasuoka Exp $

PROG=	login_radius
SRCS=	login_radius.c raddauth.c
MAN=	login_radius.8
DPADD=	${LIBUTIL} ${LIBCRYPTO}
LDADD=	-lutil -lcrypto
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	_radius
BINMODE=2555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
