#	$OpenBSD: Makefile,v 1.1 2003/07/12 06:31:49 jason Exp $

PROG=asi
NOMAN=
SRCS=fpregs.S asi.c asi_asm.S
CFLAGS+=-I${.CURDIR}/../goodfreg

.PATH: ${.CURDIR}/../goodfreg

.include <bsd.regress.mk>
