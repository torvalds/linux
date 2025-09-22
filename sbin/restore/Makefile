#	$OpenBSD: Makefile,v 1.6 2016/03/30 06:38:43 jmc Exp $

PROG=	restore
LINKS=	${BINDIR}/restore ${BINDIR}/rrestore
CFLAGS+=-DRRESTORE
SRCS=	main.c interactive.c restore.c dirs.c symtab.c tape.c utilities.c \
	dumprmt.c
MAN=	restore.8
.PATH:	${.CURDIR}/../dump

.include <bsd.prog.mk>
