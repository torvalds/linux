#	$OpenBSD: Makefile,v 1.17 2015/09/25 03:17:02 guenther Exp $

.include <bsd.own.mk>

PROG=	config
SRCS=	files.c gram.y hash.c main.c mkheaders.c mkioconf.c mkmakefile.c \
	mkswap.c pack.c scan.l sem.c util.c \
	ukc.c misc.c ukcutil.c cmd.c
SRCS+=  exec_elf.c

CFLAGS+=-I${.CURDIR} -I.

LDADD=	-lkvm
DPADD=	${LIBKVM}

MAN=	config.8

.include <bsd.prog.mk>
