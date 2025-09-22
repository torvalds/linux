#	$OpenBSD: Makefile,v 1.10 2018/01/06 16:26:12 millert Exp $

PROG=	init
MAN=	init.8
DPADD=	${LIBUTIL}
LDADD=	-lutil
CFLAGS+=-DDEBUGSHELL -DSECURE

.include <bsd.prog.mk>
