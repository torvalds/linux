#	$OpenBSD: Makefile,v 1.12 2015/11/27 09:37:56 tb Exp $
#	@(#)Makefile	8.1 (Berkeley) 5/31/93

PROG=	snake
SRCS=	snake.c
MAN=	snake.6
DPADD=	${LIBM} ${LIBCURSES}
LDADD=	-lm -lcurses

.include <bsd.prog.mk>
