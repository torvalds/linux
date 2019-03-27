# Make `routed` for FreeBSD
# $FreeBSD$

PACKAGE=runtime
PROG=	routed
MAN=	routed.8
SRCS=	if.c input.c main.c output.c parms.c radix.c rdisc.c table.c trace.c
WARNS?=	3
LIBADD=	md

SUBDIR= rtquery

.include <bsd.prog.mk>
