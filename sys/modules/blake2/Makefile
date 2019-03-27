# $FreeBSD$

.PATH:	${SRCTOP}/sys/contrib/libb2
.PATH:	${SRCTOP}/sys/crypto/blake2
.PATH:	${SRCTOP}/sys/opencrypto
.PATH:	${SRCTOP}/contrib/llvm/tools/clang/lib/Headers

KMOD	= blake2

# Vendor sources
SRCS	+= blake2.h
SRCS	+= blake2-impl.h
SRCS	+= blake2-config.h

SRCS	+= blake2-dispatch.c
SRCS	+= blake2bp.c
SRCS	+= blake2sp.c

CFLAGS	+= -I${SRCTOP}/sys/contrib/libb2
CFLAGS	+= -I${SRCTOP}/sys/crypto/blake2

# x86 specific optimization headers:
SRCS	+= blake2b-load-sse2.h
SRCS	+= blake2b-load-sse41.h
SRCS	+= blake2b-round.h

SRCS	+= blake2s-load-sse2.h
SRCS	+= blake2s-load-sse41.h
SRCS	+= blake2s-load-xop.h
SRCS	+= blake2s-round.h

# C files to build a dispatched fat implementation on x86
SRCS_IN	+= blake2b-sse2.c
SRCS_IN	+= blake2b-ssse3.c
SRCS_IN	+= blake2b-sse41.c
SRCS_IN	+= blake2b-avx.c
SRCS_IN	+= blake2b-xop.c
SRCS_IN	+= blake2s-sse2.c
SRCS_IN	+= blake2s-ssse3.c
SRCS_IN	+= blake2s-sse41.c
SRCS_IN	+= blake2s-avx.c
SRCS_IN	+= blake2s-xop.c

OBJS+=	${SRCS_IN:S/.c/.o/g}

CFLAGS.blake2b-sse2.c	+= -DSUFFIX=_sse2 -msse2
CFLAGS.blake2s-sse2.c	+= -DSUFFIX=_sse2 -msse2

CFLAGS.blake2b-ssse3.c	+= -DSUFFIX=_ssse3 -msse2 -mssse3
CFLAGS.blake2s-ssse3.c	+= -DSUFFIX=_ssse3 -msse2 -mssse3

CFLAGS.blake2b-sse41.c	+= -DSUFFIX=_sse41 -msse2 -mssse3 -msse4.1
CFLAGS.blake2s-sse41.c	+= -DSUFFIX=_sse41 -msse2 -mssse3 -msse4.1

CFLAGS.blake2b-avx.c	+= -DSUFFIX=_avx -msse2 -mssse3 -msse4.1 -mavx
CFLAGS.blake2s-avx.c	+= -DSUFFIX=_avx -msse2 -mssse3 -msse4.1 -mavx

CFLAGS.blake2b-xop.c	+= -DSUFFIX=_xop -msse2 -mssse3 -msse4.1 -mavx -mxop
CFLAGS.blake2s-xop.c	+= -DSUFFIX=_xop -msse2 -mssse3 -msse4.1 -mavx -mxop

.for src in ${SRCS_IN}
${src:S/.c/.o/}: ${src}
	${CC} -c ${CFLAGS:N-nostdinc} ${CFLAGS.${src}} ${WERROR} ${PROF} \
	    -D_MM_MALLOC_H_INCLUDED -Wno-unused-function ${.IMPSRC}
	${CTFCONVERT_CMD}

${src:S/.c/.o/}: intrin.h emmintrin.h tmmintrin.h smmintrin.h immintrin.h \
    x86intrin.h ${SRCS:M*.h}
.endfor

# FreeBSD-specific sources:
SRCS	+= blake2_kfreebsd.h
SRCS	+= config.h

SRCS	+= stddef.h
SRCS	+= stdint.h
SRCS	+= stdio.h
SRCS	+= stdlib.h
SRCS	+= string.h

SRCS	+= blake2_cryptodev.c

SRCS	+= opt_param.h cryptodev_if.h bus_if.h device_if.h

WARNS	?= 6

.include <bsd.kmod.mk>

CWARNFLAGS.blake2-dispatch.c	+=	-Wno-unused-const-variable
CWARNFLAGS.blake2s-ref.c	+= 	-Wno-cast-qual -Wno-unused-function
CWARNFLAGS			+=	-Wno-cast-qual
