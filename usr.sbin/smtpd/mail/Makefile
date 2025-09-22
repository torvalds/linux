#	$OpenBSD: Makefile,v 1.8 2018/07/25 10:19:28 gilles Exp $
.PATH:	${.CURDIR}/..

PROGS=	mail.lmtp mail.maildir mail.mboxfile mail.mda

MAN=	mail.lmtp.8 mail.maildir.8 mail.mboxfile.8 mail.mda.8

BINOWN=	root
BINGRP=	wheel

BINDIR=	/usr/libexec

CFLAGS+=	-fstack-protector-all
CFLAGS+=	-Wall -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CFLAGS+=	-Werror-implicit-function-declaration

.include <bsd.prog.mk>
