#	$OpenBSD: Makefile,v 1.3 2014/11/03 16:50:20 tedu Exp $

PROG=	login_reject
SRCS=	login_reject.c
MAN=	login_reject.8
CFLAGS+=-Wall

BINOWN=	root
BINGRP=	auth
BINMODE=555
BINDIR= /usr/libexec/auth

.include <bsd.prog.mk>
