# $FreeBSD$

SYSDIR?=	${SRCTOP}/sys

ARCHDIR=	${MACHINE_CPUARCH}

.PATH: ${SYSDIR}/cddl/contrib/opensolaris/uts/common/dtrace
.PATH: ${SYSDIR}/cddl/compat/opensolaris/kern
.PATH: ${SYSDIR}/cddl/kern
.PATH: ${SYSDIR}/cddl/dev/dtrace
.PATH: ${SYSDIR}/cddl/dev/dtrace/${ARCHDIR}

KMOD=		dtrace
SRCS=		dtrace.c \
		dtrace_xoroshiro128_plus.c \
		dtrace_asm.S \
		dtrace_subr.c

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
.PATH: ${SYSDIR}/cddl/dev/dtrace/x86
SRCS+=		dis_tables.c \
		instr_size.c
CFLAGS+=	-I${SYSDIR}/cddl/contrib/opensolaris/uts/intel \
		-I${SYSDIR}/cddl/dev/dtrace/x86
.endif

SRCS+=		bus_if.h device_if.h vnode_if.h

# Needed for dtrace_asm.S
DPSRCS+=	assym.inc

# These are needed for assym.inc
SRCS+=		opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h

#This is needed for dtrace.c
SRCS += 	opensolaris_taskq.c

.if ${MACHINE_CPUARCH} == "i386"
SRCS+=		opt_apic.h
.endif

CFLAGS+=	-I${SYSDIR}/cddl/compat/opensolaris \
		-I${SYSDIR}/cddl/dev/dtrace \
		-I${SYSDIR}/cddl/dev/dtrace/${ARCHDIR} \
		-I${SYSDIR}/cddl/contrib/opensolaris/uts/common \
		-I${SYSDIR}/cddl/contrib/opensolaris/uts/common/dtrace \
		-I${SYSDIR}/cddl/contrib/opensolaris/common/util \
		-I${SYSDIR} -DDIS_MEM

CFLAGS+=	-DSMP

EXPORT_SYMS=	dtrace_register \
		dtrace_unregister \
		dtrace_probe_lookup

dtrace_asm.o:  assym.inc

.include <bsd.kmod.mk>

CFLAGS+=	-include ${SYSDIR}/cddl/compat/opensolaris/sys/debug_compat.h

CWARNFLAGS+=	-Wno-parentheses
CWARNFLAGS+=	-Wno-uninitialized
CWARNFLAGS+=	-Wno-cast-qual
CWARNFLAGS+=	-Wno-unused
