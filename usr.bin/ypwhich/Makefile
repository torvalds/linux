#	from: @(#)Makefile	5.8 (Berkeley) 7/28/90
# $FreeBSD$

YPSERV=${SRCTOP}/usr.sbin/ypserv/common
.PATH: ${YPSERV}

PROG=	ypwhich
SRCS=	yplib_host.c ypwhich.c

CFLAGS+=	-I${YPSERV} -I.

WARNS?=	2

.include <bsd.prog.mk>
