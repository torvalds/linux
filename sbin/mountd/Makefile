#	$OpenBSD: Makefile,v 1.5 2022/08/29 17:00:30 deraadt Exp $

PROG=	mountd
MAN=	exports.5 mountd.8

LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>

# Don't compile mountd as static binary by default
LDSTATIC=       
