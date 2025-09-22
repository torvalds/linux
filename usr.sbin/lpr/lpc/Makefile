#	from: @(#)Makefile	8.1 (Berkeley) 6/6/93
#	$OpenBSD: Makefile,v 1.3 2002/05/20 23:13:50 millert Exp $

PROG=	lpc
CFLAGS+=-I${.CURDIR}/../common_source
MAN=	lpc.8
SRCS=	lpc.c cmds.c cmdtab.c startdaemon.c common.c common_vars.c
BINGRP=	daemon
BINMODE=2555
.PATH:	${.CURDIR}/../common_source

.include "../../Makefile.inc"
.include <bsd.prog.mk>
