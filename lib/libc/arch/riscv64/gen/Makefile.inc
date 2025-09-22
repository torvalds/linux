# $OpenBSD: Makefile.inc,v 1.2 2021/06/19 18:43:28 deraadt Exp $
SRCS+=	ldexp.c
SRCS+=	infinity.c modf.c nan.c
SRCS+=	flt_rounds.c
SRCS+=	fpclassifyl.c
SRCS+=	fpgetmask.c fpgetround.c fpgetsticky.c
SRCS+=	fpsetmask.c fpsetround.c fpsetsticky.c
SRCS+=	fabs.c usertc.c
SRCS+=	setjmp.S _setjmp.S sigsetjmp.S
SRCS+=	isnanl.c signbitl.c isfinitel.c isinfl.c isnormall.c
