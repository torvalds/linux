# $OpenBSD: Makefile,v 1.4 2025/08/21 15:26:58 rsadowski Exp $

.include <bsd.own.mk>

USE_UNWIND=no
.if ${COMPILER_VERSION} == "clang"
USE_UNWIND=yes
.endif

LIB=execinfo
SRCS=backtrace.c
MAN= backtrace.3

.if empty(CFLAGS:M-std=*)
CFLAGS+=  -std=gnu99
.endif

.if ${USE_UNWIND} == "yes"
.PATH: ${SRCDIR} ${BSDSRCDIR}/gnu/llvm/libunwind/src
.PATH: ${SRCDIR} ${BSDSRCDIR}/gnu/llvm/libcxx/src
CXXFLAGS+= -I${BSDSRCDIR}/gnu/llvm/libunwind/include \
           -I${BSDSRCDIR}/gnu/lib/libcxx/include/c++/v1/ \
           -I${BSDSRCDIR}/gnu/llvm/libcxx/include
CFLAGS+= -I${BSDSRCDIR}/gnu/llvm/libunwind/include

CPPFLAGS+=  -D_LIBUNWIND_IS_NATIVE_ONLY
CPPFLAGS+=  -DLIBUNWIND_USE_WEAK_PTHREAD
CPPFLAGS+=  -DNDEBUG
CXXFLAGS+=  -nostdlib -nostdinc++ -funwind-tables \
            -fno-exceptions -fno-rtti
.if empty(CXXFLAGS:M-std=*)
CXXFLAGS+=  -std=c++20
.endif

SRCS+=unwind.c \
	Unwind-EHABI.cpp \
	Unwind-sjlj.c \
	UnwindLevel1-gcc-ext.c \
	UnwindLevel1.c \
	UnwindRegistersRestore.S \
	UnwindRegistersSave.S \
	libunwind.cpp \
	new_handler.cpp

.else # !${USE_UNWIND}
SRCS+=builtin.c
.endif

VERSION_SCRIPT=	${.CURDIR}/Symbols.map

includes:
	cmp -s ${DESTDIR}/usr/include/execinfo.h ${.CURDIR}/execinfo.h || \
		${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) \
		${.CURDIR}/execinfo.h ${DESTDIR}/usr/include/execinfo.h

.include <bsd.lib.mk>
