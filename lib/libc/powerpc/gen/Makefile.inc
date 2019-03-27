# $FreeBSD$

.include "${LIBC_SRCTOP}/powerpc/gen/Makefile.common"

SRCS += fabs.S flt_rounds.c fpgetmask.c fpgetround.c \
	fpgetsticky.c fpsetmask.c fpsetround.c \
	_setjmp.S setjmp.S sigsetjmp.S
