# $FreeBSD$

.include <src.opts.mk>

PROG=	mtest
MAN=	mtest.8

BINMODE= 555
WARNS?=	3

# XXX This assumes INET support in the base system.
CFLAGS+=-DINET

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif

.include <bsd.prog.mk>
