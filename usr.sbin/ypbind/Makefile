#	$OpenBSD: Makefile,v 1.5 2015/09/09 15:33:18 deraadt Exp $

PROG=	ypbind
MAN=	ypbind.8
CFLAGS+=-DDAEMON -DHEURISTIC

LDADD=-lrpcsvc
DPADD=${LIBRPCSVC}

.include <bsd.prog.mk>
