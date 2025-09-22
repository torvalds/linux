#	$OpenBSD: Makefile,v 1.2 2006/11/22 07:36:01 ray Exp $
#	$FreeBSD: Makefile,v 1.3 1997/02/22 14:22:00 peter Exp $

PROG=	revnetgroup
SRCS=	revnetgroup.c hash.c parse_netgroup.c

MAN= revnetgroup.8

LDADD+=	-lutil
DPADD+=	${LIBUTIL}

.include <bsd.prog.mk>
