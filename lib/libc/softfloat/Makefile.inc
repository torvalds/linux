#	$NetBSD: Makefile.inc,v 1.10 2011/07/04 02:53:15 mrg Exp $
# $FreeBSD$

SOFTFLOAT_BITS?=64
.PATH:		${LIBC_ARCH}/softfloat \
		${LIBC_SRCTOP}/softfloat/bits${SOFTFLOAT_BITS} \
		${LIBC_SRCTOP}/softfloat

CFLAGS+=	-I${LIBC_SRCTOP}/${LIBC_ARCH}/softfloat \
		-I${LIBC_SRCTOP}/softfloat
CFLAGS+=	-DSOFTFLOAT_FOR_GCC

SRCS+=		softfloat.c

# Deprecated FPU control interface
.if ${LIBC_ARCH} != "riscv"
SRCS+=		fpgetround.c fpsetround.c fpgetmask.c fpsetmask.c \
		fpgetsticky.c
.endif

SRCS+=		eqsf2.c nesf2.c gtsf2.c gesf2.c ltsf2.c lesf2.c negsf2.c \
		eqdf2.c nedf2.c gtdf2.c gedf2.c ltdf2.c ledf2.c negdf2.c \
		unordsf2.c unorddf2.c

.if defined(SOFTFLOAT_128)
CFLAGS+=	-DFLOAT128
SRCS+=		eqtf2.c netf2.c gttf2.c getf2.c lttf2.c letf2.c negtf2.c
.endif

.if defined(SOFTFLOAT_X80)
CFLAGS+=	-DFLOATX80
SRCS+=		nexf2.c gtxf2.c gexf2.c negxf2.c
.endif

SYM_MAPS+=	${LIBC_SRCTOP}/softfloat/Symbol.map
