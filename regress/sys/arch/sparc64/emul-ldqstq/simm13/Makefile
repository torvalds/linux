#	$OpenBSD: Makefile,v 1.1 2003/07/13 06:33:24 jason Exp $

PROG=simm13
NOMAN=
SRCS=simm13_asm.S simm13.c fpregs.S
CFLAGS+=-I${.CURDIR}/../goodfreg -O0 -g
CLEANFILES+=simm13_asm.S simm13.c

.PATH: ${.CURDIR}/../goodfreg

simm13_asm.S: ${.CURDIR}/genasm.pl
	perl ${.CURDIR}/genasm.pl > $@

simm13.c: ${.CURDIR}/genc.pl
	perl ${.CURDIR}/genc.pl > $@

.include <bsd.regress.mk>
