# $FreeBSD$

PACKAGE=runtime
PROG=	ping6
MAN=	ping6.8

CFLAGS+=-DIPSEC -DKAME_SCOPEID -DUSE_RFC2292BIS
WARNS?=	3

BINOWN=	root
BINMODE=4555

LIBADD=	ipsec m md

.include <bsd.prog.mk>
