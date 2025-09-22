#	$OpenBSD: Makefile.inc,v 1.14 2023/08/13 06:55:37 miod Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.c modf.c nan.c setjmp.S \
	sigsetjmp.S
SRCS+=	fpclassifyl.c isfinitel.c isinfl.c isnanl.c isnormall.c signbitl.c \
	usertc.c
SRCS+=	flt_rounds.S fpgetmask.S fpgetround.S fpgetsticky.S fpsetmask.S \
	fpsetround.S fpsetsticky.S
