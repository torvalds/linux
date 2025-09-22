#	$OpenBSD: Makefile,v 1.9 2015/11/26 19:01:47 deraadt Exp $

.PATH:  ${.CURDIR}/../../usr.bin/passwd

PROG=	login_chpass
SRCS=	login_chpass.c
MAN=	login_chpass.8
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	auth
BINMODE=4555
BINDIR=	/usr/libexec/auth

.include <bsd.prog.mk>
