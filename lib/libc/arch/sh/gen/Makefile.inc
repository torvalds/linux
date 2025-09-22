#	$OpenBSD: Makefile.inc,v 1.11 2020/07/06 13:33:05 pirofti Exp $

SRCS+= flt_rounds.c infinity.c ldexp.c modf.c nan.c setjmp.S _setjmp.S
SRCS+= sigsetjmp.S
SRCS+=	fabs.c fpgetmask.c fpgetround.c fpgetsticky.c \
	fpsetmask.c fpsetround.c fpsetsticky.c usertc.c
