#	@(#)Makefile	8.1 (Berkeley) 6/29/93
#	$FreeBSD$

PROG=	gprof
SRCS=	gprof.c aout.c arcs.c dfn.c elf.c lookup.c hertz.c \
	printgprof.c printlist.c kernel.c
FILES=	gprof.flat gprof.callg
FILESDIR=	${SHAREDIR}/misc

WARNS?=	1

.include <bsd.prog.mk>
