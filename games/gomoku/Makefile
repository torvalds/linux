#	$OpenBSD: Makefile,v 1.5 2002/05/23 18:43:00 deraadt Exp $

PROG=	gomoku
SRCS=	bdinit.c bdisp.c main.c makemove.c pickmove.c stoc.c
MAN=	gomoku.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
