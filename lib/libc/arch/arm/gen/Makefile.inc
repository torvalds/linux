# $OpenBSD: Makefile.inc,v 1.17 2024/04/15 15:21:11 naddy Exp $
# $NetBSD: Makefile.inc,v 1.6 2003/08/01 17:03:47 lukem Exp $

SRCS+=	divsi3.S fabs.c flt_rounds.c infinity.c
SRCS+=	ldexp.c modf.c nan.c usertc.c
SRCS+=	setjmp.S _setjmp.S sigsetjmp.S
