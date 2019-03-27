# $FreeBSD$

GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs
SRCDIR=	${GCCLIB}/libgomp

.PATH: ${SRCDIR} ${SRCDIR}/config/posix

LIB=		gomp
SHLIB_MAJOR=	1

SRCS=	alloc.c barrier.c critical.c env.c \
	error.c iter.c loop.c ordered.c parallel.c sections.c \
	single.c team.c work.c lock.c mutex.c proc.c sem.c \
	bar.c time.c fortran.c affinity.c
SRCS+=	gstdint.h libgomp_f.h omp.h omp_lib.h

INCS+=	omp.h

CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -I${.CURDIR} -I. -I${SRCDIR} -I${SRCDIR}/config/posix

VERSION_MAP=	${SRCDIR}/libgomp.map

# Target-specific OpenMP configuration
.if ${MACHINE_CPUARCH} == arm || ${MACHINE_CPUARCH} == i386 || \
    ${MACHINE_ARCH} == powerpc || ${MACHINE_ARCH} == powerpcspe || \
    (${MACHINE_CPUARCH} == mips &&  ${MACHINE_ARCH:Mmips64*} == "")
OMP_LOCK_ALIGN	=	4
OMP_LOCK_KIND=		4
OMP_LOCK_SIZE=		4
OMP_NEST_LOCK_ALIGN=	4
OMP_NEST_LOCK_KIND=	8
OMP_NEST_LOCK_SIZE=	8
.else
OMP_LOCK_ALIGN	=	8
OMP_LOCK_KIND=		8
OMP_LOCK_SIZE=		8
OMP_NEST_LOCK_ALIGN=	8
OMP_NEST_LOCK_KIND=	8
OMP_NEST_LOCK_SIZE=	16
.endif

gstdint.h:
	echo  '#include <sys/types.h>'	>  ${.TARGET}
	echo  '#include <stdint.h>'	>> ${.TARGET}
CLEANFILES+= gstdint.h

.for HFILE in libgomp_f.h omp.h omp_lib.h
${HFILE}: ${SRCDIR}/${HFILE}.in ${.CURDIR}/Makefile
	sed -e 's/@OMP_LOCK_ALIGN@/${OMP_LOCK_ALIGN}/g' \
	    -e 's/@OMP_LOCK_KIND@/${OMP_LOCK_KIND}/g' \
	    -e 's/@OMP_LOCK_SIZE@/${OMP_LOCK_SIZE}/g' \
	    -e 's/@OMP_NEST_LOCK_ALIGN@/${OMP_NEST_LOCK_ALIGN}/g' \
	    -e 's/@OMP_NEST_LOCK_KIND@/${OMP_NEST_LOCK_KIND}/g' \
	    -e 's/@OMP_NEST_LOCK_SIZE@/${OMP_NEST_LOCK_SIZE}/g' \
	    < ${SRCDIR}/${HFILE}.in > ${.TARGET}
CLEANFILES+= ${HFILE}
.endfor

.include <bsd.lib.mk>
