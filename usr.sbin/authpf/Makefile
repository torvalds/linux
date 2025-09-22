#	$OpenBSD: Makefile,v 1.14 2016/03/30 06:38:46 jmc Exp $

PROG=	authpf
MAN=	authpf.8

LINKS=  ${BINDIR}/authpf ${BINDIR}/authpf-noip

BINOWN= root
BINGRP= authpf
BINMODE= 6555
SRCS=	authpf.c
CFLAGS+= -Wall

.include <bsd.prog.mk>
