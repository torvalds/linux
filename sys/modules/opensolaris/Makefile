# $FreeBSD$

SYSDIR?=	${SRCTOP}/sys

.PATH:		${SYSDIR}/cddl/compat/opensolaris/kern

KMOD=		opensolaris
SRCS=		opensolaris.c		\
		opensolaris_cmn_err.c	\
		opensolaris_kmem.c	\
		opensolaris_misc.c	\
		opensolaris_proc.c	\
		opensolaris_sunddi.c

_A=${SYSDIR}/cddl/contrib/opensolaris/common/atomic
.if exists(${_A}/${MACHINE_CPUARCH}/opensolaris_atomic.S)
.PATH:	${_A}/${MACHINE_CPUARCH}
SRCS+=		opensolaris_atomic.S
.elif exists(${_A}/${MACHINE_ARCH}/opensolaris_atomic.S)
.PATH:	${_A}/${MACHINE_ARCH}
SRCS+=		opensolaris_atomic.S
.else
SRCS+=		opensolaris_atomic.c
.endif

CFLAGS+=	-I${SYSDIR}/cddl/compat/opensolaris		\
		-I${SYSDIR}/cddl/contrib/opensolaris/uts/common	\
		-I${SYSDIR}

IGNORE_PRAGMA=	1

.include <bsd.kmod.mk>

CFLAGS+=	-include ${SYSDIR}/cddl/compat/opensolaris/sys/debug_compat.h
