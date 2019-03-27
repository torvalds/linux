# $FreeBSD$

.include <src.opts.mk>

PROG=	so_setfib
MAN=
WARNS?=	6

.if ${MK_INET} != "no"
CFLAGS+=	-DINET
.endif
.if ${MK_INET6} != "no"
CFLAGS+=	-DINET6
.endif

.include <bsd.prog.mk>
