# $OpenBSD: Makefile.inc,v 1.4 2015/09/13 14:21:46 miod Exp $
SRCS += eqdf2.c eqsf2.c fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c
SRCS += fpsetround.c fpsetsticky.c gedf2.c gesf2.c gtdf2.c gtsf2.c ledf2.c
SRCS += lesf2.c ltdf2.c ltsf2.c nedf2.c negdf2.c negsf2.c nesf2.c
#SRCS += timesoftfloat.c
SRCS += unorddf2.c unordsf2.c
SRCS += softfloat.c
CFLAGS += -DSOFTFLOAT_FOR_GCC
CFLAGS += -I${LIBCSRCDIR}/softfloat

.PATH: ${.CURDIR}/softfloat
