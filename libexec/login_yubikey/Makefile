#	$OpenBSD: Makefile,v 1.2 2012/01/31 16:58:38 sobrado Exp $

PROG=	login_yubikey
MAN=	login_yubikey.8
SRCS=	login_yubikey.c yubikey.c
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	auth
BINMODE=2555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
