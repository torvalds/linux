# $FreeBSD$

PROG=	wlanwatch
BINDIR=	/usr/local/bin
MAN=

.include <src.opts.mk>

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif

.include <bsd.prog.mk>
