# $FreeBSD$
#
# All library objects contain FreeBSD revision strings by default; they may be
# excluded as a space-saving measure.  To produce a library that does
# not contain these strings, add -DSTRIP_FBSDID (see <sys/cdefs.h>) to CFLAGS
# below.

PACKAGE=	clibs
SHLIBDIR?= /lib

.include <src.opts.mk>
MK_BIND_NOW= no
MK_SSP=	no

LIB=thr
SHLIB_MAJOR= 3
NO_WTHREAD_SAFETY=1
NO_WCAST_ALIGN.gcc=1    # for gcc 4.2
CFLAGS+=-DPTHREAD_KERNEL
CFLAGS+=-I${SRCTOP}/lib/libc/include -I${.CURDIR}/thread \
	-I${SRCTOP}/include
CFLAGS+=-I${.CURDIR}/arch/${MACHINE_CPUARCH}/include
CFLAGS+=-I${.CURDIR}/sys
CFLAGS+=-I${SRCTOP}/libexec/rtld-elf
CFLAGS+=-I${SRCTOP}/libexec/rtld-elf/${MACHINE_CPUARCH}
CFLAGS+=-I${SRCTOP}/lib/libthread_db
CFLAGS+=-Winline

CFLAGS.thr_stack.c+=	-Wno-cast-align
CFLAGS.rtld_malloc.c+=	-Wno-cast-align
.include <bsd.compiler.mk>
.if !(${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} < 40300)
CFLAGS.thr_symbols.c+=	-Wno-missing-variable-declarations
.endif

.ifndef NO_THREAD_UNWIND_STACK
CFLAGS+=-fexceptions
CFLAGS+=-D_PTHREAD_FORCED_UNWIND
.endif

LDFLAGS+=-Wl,-znodelete

VERSION_DEF=${SRCTOP}/lib/libc/Versions.def
SYMBOL_MAPS=${.CURDIR}/pthread.map

MAN=	libthr.3

# enable extra internal consistency checks
CFLAGS+=-D_PTHREADS_INVARIANTS

PRECIOUSLIB=

.PATH: ${.CURDIR}/arch/${MACHINE_CPUARCH}/${MACHINE_CPUARCH}
.PATH: ${SRCTOP}/libexec/rtld-elf

.if exists(${.CURDIR}/arch/${MACHINE_CPUARCH}/Makefile.inc)
.include "${.CURDIR}/arch/${MACHINE_CPUARCH}/Makefile.inc"
.endif
.include "${.CURDIR}/sys/Makefile.inc"
.include "${.CURDIR}/thread/Makefile.inc"
SRCS+= rtld_malloc.c

.if ${MK_INSTALLLIB} != "no"
SYMLINKS+=lib${LIB}.a ${LIBDIR}/libpthread.a
.endif
.if !defined(NO_PIC)
SYMLINKS+=lib${LIB}.so ${LIBDIR}/libpthread.so
.endif
.if ${MK_PROFILE} != "no"
SYMLINKS+=lib${LIB}_p.a ${LIBDIR}/libpthread_p.a
.endif

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.lib.mk>
