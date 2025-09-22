#	$OpenBSD: Makefile,v 1.6 2002/05/23 18:43:00 deraadt Exp $

PROG=	mille
SRCS=	comp.c end.c extern.c init.c mille.c misc.c move.c print.c \
	roll.c save.c types.c varpush.c
DPADD=	${LIBCURSES}
LDADD=	-lcurses
MAN=	mille.6

.include <bsd.prog.mk>
