#	$OpenBSD: Makefile,v 1.8 2015/11/17 15:27:24 tedu Exp $

PROG=	tetris
SRCS=	input.c screen.c shapes.c scores.c tetris.c
MAN=	tetris.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
