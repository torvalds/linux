#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	kvm

SHLIBDIR?= /lib
SHLIB_MAJOR=	7
CFLAGS+=-DNO__SCCSID -I${.CURDIR}

WARNS?=	6

SRCS=	kvm.c kvm_cptime.c kvm_getloadavg.c \
	kvm_getswapinfo.c kvm_pcpu.c kvm_private.c kvm_proc.c kvm_vnet.c \
	kvm_minidump_aarch64.c \
	kvm_amd64.c kvm_minidump_amd64.c \
	kvm_arm.c kvm_minidump_arm.c \
	kvm_i386.c kvm_minidump_i386.c \
	kvm_minidump_mips.c \
	kvm_powerpc.c kvm_powerpc64.c \
	kvm_minidump_riscv.c \
	kvm_sparc64.c
INCS=	kvm.h

LIBADD=	elf

MAN=	kvm.3 kvm_getcptime.3 kvm_geterr.3 kvm_getloadavg.3 \
	kvm_getpcpu.3 kvm_getprocs.3 kvm_getswapinfo.3 kvm_native.3 \
	kvm_nlist.3 kvm_open.3 kvm_read.3

MLINKS+=kvm_getpcpu.3 kvm_getmaxcpu.3 \
	kvm_getpcpu.3 kvm_dpcpu_setcpu.3 \
	kvm_getpcpu.3 kvm_read_zpcpu.3 \
	kvm_getpcpu.3 kvm_counter_u64_fetch.3
MLINKS+=kvm_getprocs.3 kvm_getargv.3 kvm_getprocs.3 kvm_getenvv.3
MLINKS+=kvm_nlist.3 kvm_nlist2.3
MLINKS+=kvm_open.3 kvm_close.3 kvm_open.3 kvm_open2.3 kvm_open.3 kvm_openfiles.3
MLINKS+=kvm_read.3 kvm_read2.3 kvm_read.3 kvm_write.3

.include <src.opts.mk>

HAS_TESTS=
SUBDIR.${MK_TESTS}=	tests

.include <bsd.lib.mk>
