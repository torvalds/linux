#	$OpenBSD: Makefile.inc,v 1.16 2023/08/13 06:55:37 miod Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.c usertc.c \
	modf.c nan.c setjmp.S sigsetjmp.S
SRCS+=	fpclassifyl.c isfinitel.c isinfl.c isnanl.c isnormall.c signbitl.c
SRCS+=	flt_rounds.S fpgetmask.S fpgetround.S fpgetsticky.S fpsetmask.S \
	fpsetround.S fpsetsticky.S
SRCS+=	divsi3.S fixdfsi.S fixunsdfsi.S udivsi3.S
