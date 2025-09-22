#	$OpenBSD: Makefile,v 1.3 2001/11/07 19:03:22 deraadt Exp $

PROG=	yppush
SRCS=	yppush.c yplib_host.c yppush_svc.c yppush_proc.c ypdb.c yppush_err.c
MAN=	yppush.8
LDADD=	-lrpcsvc
DPADD=	${LIBRPCSVC}
.PATH:	${.CURDIR}/../common ${.CURDIR}/../ypserv
CFLAGS+=-I${.CURDIR}/../common

.include <bsd.prog.mk>
