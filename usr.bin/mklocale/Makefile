#	@(#)Makefile	8.1 (Berkeley) 6/7/93
# $FreeBSD$

PROG=	mklocale
SRCS=	yacc.y lex.l y.tab.h
CFLAGS+= -I. -I${.CURDIR} -I${SRCTOP}/lib/libc/locale

NO_WMISSING_VARIABLE_DECLARATIONS=

.include <bsd.prog.mk>
