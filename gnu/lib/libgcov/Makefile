# $FreeBSD$

MK_PROFILE=	no
MK_SSP=		no

.include <src.opts.mk>

GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs
CCDIR=	${SRCTOP}/gnu/usr.bin/cc
.include "${CCDIR}/Makefile.tgt"
.include "${CCDIR}/cc_tools/Makefile.hdrs"

.PATH: ${GCCDIR}/config/${GCC_CPU} ${GCCDIR}

LIB=		gcov

CFLAGS+=	-DIN_GCC -DIN_LIBGCC2 -D__GCC_FLOAT_NOT_NEEDED
CFLAGS+=	-D_PTHREADS -DGTHREAD_USE_WEAK
CFLAGS+=	-I${CCDIR}/cc_tools \
		-I${GCCLIB}/include -I${GCCDIR}/config -I${GCCDIR} -I.

.if ${TARGET_CPUARCH} == "arm"
CFLAGS+=	-DTARGET_ARM_EABI
.endif

#
# Library members defined in libgcov.c.
# Defined in libgcov.c, included only in gcov library
SYMS = _gcov _gcov_merge_add _gcov_merge_single _gcov_merge_delta \
    _gcov_fork _gcov_execl _gcov_execlp _gcov_execle \
    _gcov_execv _gcov_execvp _gcov_execve \
    _gcov_interval_profiler _gcov_pow2_profiler _gcov_one_value_profiler

OBJS=		${SYMS:S/$/.o/}
OBJS_T=		${SYMS:S/$/.o/}
OBJS_P=		${SYMS:S/$/.po/}
OBJS_S=		${SYMS:S/$/.pico/}

#-----------------------------------------------------------------------
#
# Helpful shortcuts for compiler invocations.
#
CC_T =	${CC} -c ${CFLAGS} -fPIC
CC_P =	${CC} -c ${CFLAGS} -p -fPIC
CC_S =	${CC} -c ${CFLAGS} ${PICFLAG} -DSHARED

COMMONHDRS=	tm.h tconfig.h gcov-iov.h options.h
CLEANFILES+=	${COMMONHDRS} cs-tm.h cs-tconfig.h options.h optionlist

${OBJS} beforedepend: ${COMMONHDRS}

${OBJS_T}: libgcov.c
	${CC_T} -DL${.PREFIX} -o ${.TARGET} ${.ALLSRC:M*.c}

.if !defined(NO_PIC)
${OBJS_S}: libgcov.c
	${CC_S} -DL${.PREFIX} -o ${.TARGET} ${.ALLSRC:M*.c}
.endif

.if ${MK_PROFILE} != "no"
${OBJS_P}: libgcov.c
	${CC_P} -DL${.PREFIX} -o ${.TARGET} ${.ALLSRC:M*.c}
.endif

.include <bsd.lib.mk>
