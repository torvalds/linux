#	$OpenBSD: Makefile.inc,v 1.14 2023/08/13 06:55:37 miod Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.S modf.c nan.c usertc.c
SRCS+=	flt_rounds.c fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c \
	fpsetround.c fpsetsticky.c
SRCS+=	fpclassifyl.c isfinitel.c isinfl.c isnanl.c isnormall.c signbitl.c
SRCS+=	setjmp.S sigsetjmp.S
SRCS+=	cacheflush.c fpc_csr.c
