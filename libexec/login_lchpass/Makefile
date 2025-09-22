#	$OpenBSD: Makefile,v 1.4 2015/10/22 12:32:33 tedu Exp $

#	BSDI	$From: Makefile,v 1.2 1997/08/08 18:58:22 prb Exp $

PROG=	login_lchpass
SRCS=	login_lchpass.c local_passwd.c pwd_check.c
MAN=	login_lchpass.8
.PATH:	${.CURDIR}/../../usr.bin/passwd

CFLAGS+=-Wall -Wno-unused
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

BINOWN=	root
BINGRP= auth
BINMODE=4555
BINDIR= /usr/libexec/auth

.include <bsd.prog.mk>
