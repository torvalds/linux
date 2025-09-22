#	$OpenBSD: Makefile,v 1.16 2017/07/10 19:52:09 espie Exp $

PROG=	route
MAN=	route.8
SRCS=	route.c show.c

CFLAGS+=	-Wall -DBFD

# XXX keywords.h is generated from keywords.sh, but see distrib/special
# easier to keep the copy in source than over-reach from there.
#
route.o tags: keywords.h

.include <bsd.prog.mk>
