#	$OpenBSD: Makefile,v 1.12 2019/02/26 07:14:35 tb Exp $

PROG=	robots
CFLAGS+=-DMAX_PER_UID=5
SRCS=	extern.c init_field.c main.c make_level.c move.c move_robs.c \
	play_level.c query.c rnd_pos.c score.c
MAN=	robots.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses

.include <bsd.prog.mk>
