#	$OpenBSD: Makefile.inc,v 1.17 2023/08/13 06:55:37 miod Exp $

SRCS+=	_setjmp.S fabs.S fixunsdfsi.S flt_rounds.c fpclassifyl.c \
	fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c \
	fpsetround.c fpsetsticky.c infinity.c isfinitel.c \
	isinfl.c isnanl.c isnormall.c ldexp.c usertc.c modf.c \
	mul.S nan.c setjmp.S signbitl.c sigsetjmp.S umul.S

CFLAGS += -Wa,-Av9b
