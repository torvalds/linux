#	$OpenBSD: Makefile.inc,v 1.17 2020/07/06 13:33:05 pirofti Exp $
#	$NetBSD: Makefile.inc,v 1.3 1995/04/10 21:09:06 jtc Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.c modf.c nan.c usertc.c
SRCS+=	flt_rounds.c fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c \
	fpsetround.c fpsetsticky.c
SRCS+=	setjmp.S sigsetjmp.S
SRCS+=	divsi3.S udivsi3.S modsi3.S umodsi3.S
