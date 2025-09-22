#	$OpenBSD: Makefile,v 1.7 2014/07/16 20:00:14 okan Exp $

PROG=	hangman
SRCS=	endgame.c extern.c getguess.c getword.c ksyms.c main.c playgame.c \
	prdata.c prman.c prword.c setup.c
MAN=	hangman.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
