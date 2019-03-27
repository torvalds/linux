# $FreeBSD$

SYSDIR?=	${SRCTOP}/sys

.PATH:		${SYSDIR}/cddl/contrib/opensolaris/uts/common/dtrace

KMOD=		fasttrap
SRCS=		fasttrap.c fasttrap_isa.c
SRCS+=		vnode_if.h

CFLAGS+=	-I${SYSDIR}/cddl/compat/opensolaris \
		-I${SYSDIR}/cddl/contrib/opensolaris/uts/common \
		-I${SYSDIR}/cddl/contrib/opensolaris/uts/common/dtrace \
		-I${SYSDIR}

.if ${MACHINE_CPUARCH} == "amd64" ||  ${MACHINE_CPUARCH} == "i386"
CFLAGS+=	-I${SYSDIR}/cddl/contrib/opensolaris/uts/intel
.PATH:		${SYSDIR}/cddl/contrib/opensolaris/uts/intel/dtrace
.elif ${MACHINE_CPUARCH} == "powerpc"
CFLAGS+=	-I${SYSDIR}/cddl/contrib/opensolaris/uts/powerpc
.PATH:		${SYSDIR}/cddl/contrib/opensolaris/uts/powerpc/dtrace
.endif

.PATH:		${SYSDIR}/cddl/contrib/opensolaris/common/unicode
SRCS+=		u8_textprep.c

CFLAGS+=	-DSMP

.include <bsd.kmod.mk>

CFLAGS+=	-include ${SYSDIR}/cddl/compat/opensolaris/sys/debug_compat.h

CWARNFLAGS+=	-Wno-cast-qual
CWARNFLAGS+=	-Wno-unused
