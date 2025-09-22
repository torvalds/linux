#	$OpenBSD: Makefile,v 1.11 2020/12/15 00:38:18 daniel Exp $

PROG=	battlestar
SRCS=	battlestar.c command1.c command2.c command3.c command4.c \
	command5.c command6.c command7.c \
	init.c cypher.c getcom.c parse.c room.c save.c fly.c misc.c \
	globals.c dayfile.c nightfile.c dayobjs.c nightobjs.c words.c
MAN=	battlestar.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
