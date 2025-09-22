#	$OpenBSD: Makefile,v 1.2 2001/01/28 19:34:28 niklas Exp $

PROG=	getty
SRCS=	main.c init.c subr.c
DPADD+=	${LIBUTIL}
LDADD+=	-lutil
MAN=	getty.8 gettytab.5 ttys.5

.include <bsd.prog.mk>
