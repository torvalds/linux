#	$OpenBSD: Makefile,v 1.3 2002/01/01 23:00:51 art Exp $
#	$NetBSD: Makefile,v 1.2 1995/04/20 22:38:44 cgd Exp $

PROG=	_setjmptest
SRCS=	jmptest.c

CFLAGS+= -DTEST_U_SETJMP

.PATH:	${.CURDIR}/../setjmp

.include <bsd.regress.mk>
