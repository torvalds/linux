#	$OpenBSD: Makefile,v 1.9 2002/05/23 18:43:00 deraadt Exp $
#	$NetBSD: Makefile,v 1.5 1995/03/21 15:06:07 cgd Exp $

PROG=	teachgammon
CFLAGS+=-I${.CURDIR}/../common_source
SRCS=	allow.c board.c check.c data.c fancy.c init.c odds.c one.c save.c \
	subs.c table.c teach.c ttext1.c ttext2.c tutor.c
DPADD=	${LIBCURSES}
LDADD=	-lcurses
NOMAN=	noman

.PATH:	${.CURDIR}/../common_source

.include "../../Makefile.inc"
.include <bsd.prog.mk>
