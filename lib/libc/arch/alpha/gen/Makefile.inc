#	$OpenBSD: Makefile.inc,v 1.14 2020/07/06 13:33:05 pirofti Exp $
#	$NetBSD: Makefile.inc,v 1.3 1995/04/29 05:09:14 cgd Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.c modf.c nan.c setjmp.S
SRCS+=	flt_rounds.c fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c \
	fpsetround.c fpsetsticky.c usertc.c
SRCS+=	sigsetjmp.S
