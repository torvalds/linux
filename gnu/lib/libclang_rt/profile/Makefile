# $OpenBSD: Makefile,v 1.5 2025/08/21 10:42:46 rsadowski Exp $

.include <bsd.own.mk>

LLVM_V=		19
LIBDIR=		/usr/lib/clang/${LLVM_V}/lib

.if ${COMPILER_VERSION:L} != "clang"
CC=		clang
CXX=		clang++
.endif

.if ${BUILD_CLANG:L} == "yes"

LIB=	clang_rt.profile
NOPIC=
NOPROFILE=

CFLAGS+=	-fPIC -std=gnu11 -fvisibility=hidden -fno-stack-protector
CFLAGS+=	-DCOMPILER_RT_HAS_ATOMICS=1
CFLAGS+=	-DCOMPILER_RT_HAS_FCNTL_LCK=1
CFLAGS+=	-DCOMPILER_RT_HAS_UNAME=1
CPPFLAGS+=	-DVISIBILITY_HIDDEN
CPPFLAGS+=	-I${BSDSRCDIR}/gnu/llvm/compiler-rt/include/

.PATH: ${.CURDIR}/../../../llvm/compiler-rt/lib/profile

SRCS+=		GCDAProfiling.c \
		InstrProfiling.c \
		InstrProfilingInternal.c \
		InstrProfilingValue.c \
		InstrProfilingBuffer.c \
		InstrProfilingFile.c \
		InstrProfilingMerge.c \
		InstrProfilingMergeFile.c \
		InstrProfilingNameVar.c \
		InstrProfilingVersionVar.c \
		InstrProfilingWriter.c \
		InstrProfilingPlatformOther.c \
		InstrProfilingRuntime.cpp \
		InstrProfilingUtil.c

beforeinstall:
	${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m ${DIRMODE} \
		${DESTDIR}${LIBDIR}

.include <bsd.lib.mk>

.else
NOPROG=
.include <bsd.prog.mk>
.endif
