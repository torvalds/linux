#	$OpenBSD: Makefile,v 1.1 2004/07/28 21:32:51 art Exp $

PROG=	itimer
SRCS=	itimer.c

real: itimer
	./itimer -r

virtual: itimer
	./itimer -v

prof: itimer
	./itimer -p

REGRESS_TARGETS=real virtual prof

.include <bsd.regress.mk>
