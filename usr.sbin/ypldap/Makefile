# $OpenBSD: Makefile,v 1.8 2015/09/09 15:33:18 deraadt Exp $
# $FreeBSD$

PROG=		ypldap
SRCS=		parse.y ypldap.c log.c	\
		ldapclient.c entries.c yp.c \
		aldap.c ber.c \
		ypldap_dns.c

MAN=		ypldap.8 ypldap.conf.5

LIBADD=		openbsd event util rpcsvc

CFLAGS+=-I${.CURDIR}
CFLAGS+=-I${SRCTOP}/contrib/pf/libevent
CFLAGS+=-I${SRCTOP}/lib/libopenbsd

WARNS=	2

.include <bsd.prog.mk>
