#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	compat
CFLAGS+=-I${SRCTOP}/lib/libc/locale
NO_PIC=

WARNS?=	0

.PATH:	${.CURDIR}/4.1 ${.CURDIR}/4.3 ${.CURDIR}/4.4

# compat 4.1 sources
SRCS+=	ftime.c

MAN+=	4.1/ftime.3

# compat 4.3 sources
SRCS+=	re_comp.c rexec.c

MAN+=	4.3/re_comp.3 4.3/rexec.3

MLINKS+=re_comp.3 re_exec.3

# compat 4.4 sources
SRCS+=	cuserid.c

MAN+=	4.4/cuserid.3

.include <bsd.lib.mk>
