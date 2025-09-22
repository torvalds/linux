#	$NetBSD: Makefile,v 1.5 1995/02/25 09:02:37 cgd Exp $
#	$OpenBSD: Makefile,v 1.5 2012/08/02 13:38:38 okan Exp $

LIB=	l
SRCS=	libmain.c libyywrap.c
NOPIC=

.PATH:	${.CURDIR}/../../usr.bin/lex

.include <bsd.own.mk>

LINKS=	${LIBDIR}/libl.a ${LIBDIR}/libfl.a
.if !defined(NOPROFILE)
LINKS+=	${LIBDIR}/libl_p.a ${LIBDIR}/libfl_p.a
.endif

.include <bsd.lib.mk>
