# $OpenBSD: Makefile,v 1.6 2025/08/21 15:26:58 rsadowski Exp $

.include <bsd.own.mk>

.if ${COMPILER_VERSION:L} != "clang"
CC=		clang
CXX=		clang++
.endif

.if ${BUILD_CLANG:L} == "yes"

HDRDIR=		${.CURDIR}/../../../gnu/llvm/libcxxabi/include
SRCDIR=		${.CURDIR}/../../../gnu/llvm/libcxxabi/src
SHDRDIR=	${.CURDIR}/../../../gnu/llvm/libcxx/include
SSHDRDIR=	${.CURDIR}/../../../gnu/llvm/libcxx/src/include
UHDRDIR=	${.CURDIR}/../../../gnu/llvm/libunwind/include
CXXINCLUDEDIR=	/usr/include/c++/v1

LOCALHDRDIR=    ${.CURDIR}/../libcxx/include/c++/v1

.PATH: ${SRCDIR} ${.CURDIR}/../../../gnu/llvm/libunwind/src

LIB=		c++abi


SRCS+=		abort_message.cpp \
		cxa_aux_runtime.cpp \
		cxa_default_handlers.cpp \
		cxa_demangle.cpp \
		cxa_exception.cpp \
		cxa_exception_storage.cpp \
		cxa_guard.cpp \
		cxa_handlers.cpp \
		cxa_personality.cpp \
		cxa_thread_atexit.cpp \
		cxa_vector.cpp \
		cxa_virtual.cpp \
		fallback_malloc.cpp \
		private_typeinfo.cpp \
		stdlib_exception.cpp \
		stdlib_new_delete.cpp \
		stdlib_stdexcept.cpp \
		stdlib_typeinfo.cpp \
		Unwind-EHABI.cpp \
		Unwind-sjlj.c \
		UnwindLevel1-gcc-ext.c \
		UnwindLevel1.c \
		UnwindRegistersRestore.S \
		UnwindRegistersSave.S \
		libunwind.cpp

CPPFLAGS+=	-Wall -I${SHDRDIR} -I${HDRDIR} -I${UHDRDIR} \
		-I${LOCALHDRDIR} -I${SHDRDIR}/../ -I${SSHDRDIR}/../
CPPFLAGS+=	-D_LIBUNWIND_IS_NATIVE_ONLY
CPPFLAGS+=	-D_LIBCXXABI_BUILDING_LIBRARY
CPPFLAGS+=	-D_LIBCPP_BUILDING_LIBRARY
CPPFLAGS+=	-D_LIBCPP_PSTL_BACKEND_STD_THREAD
CPPFLAGS+=	-D_LIBCPP_HARDENING_MODE=2
CPPFLAGS+=	-DNDEBUG
CFLAGS+=	-nostdlib -funwind-tables
CXXFLAGS+=	-nostdlib -nostdinc++ -funwind-tables
.if empty(CXXFLAGS:M-std=*)
CXXFLAGS+=	-std=c++23
.endif

STD_HEADERS=	cxxabi.h \
		__cxxabi_config.h

.for hdr in ${STD_HEADERS}
STD+=		${hdr} ${HDRDIR}/${hdr}
.endfor
STDDIR=		${CXXINCLUDEDIR}

STD_UHEADERS=	libunwind.h \
		__libunwind_config.h \
		unwind.h \
		unwind_arm_ehabi.h \
		unwind_itanium.h

.for hdr in ${STD_UHEADERS}
USTD+=		${hdr} ${UHDRDIR}/${hdr}
.endfor
USTDDIR=	${CXXINCLUDEDIR}

MACHO_HEADERS=compact_unwind_encoding.h

.for hdr in ${MACHO_HEADERS}
MACHO+=		${hdr} ${UHDRDIR}/mach-o/${hdr}
.endfor
MACHODIR=	${CXXINCLUDEDIR}/mach-o

includes:
	@echo installing ${STD_HEADERS}
	${INSTALL} -d -m 755 ${DESTDIR}/${STDDIR};
.	for hdr src in ${STD}
		cmp -s ${src} ${DESTDIR}/${STDDIR}/${hdr} || \
		${INSTALL} ${INSTALL_COPY} -m 444 ${src} ${DESTDIR}/${STDDIR}/${hdr};
.	endfor
	${INSTALL} -d -m 755 ${DESTDIR}/${USTDDIR};
.	for hdr src in ${USTD}
		cmp -s ${src} ${DESTDIR}/${USTDDIR}/${hdr} || \
		${INSTALL} ${INSTALL_COPY} -m 444 ${src} ${DESTDIR}/${USTDDIR}/${hdr};
.	endfor
	${INSTALL} -d -m 755 ${DESTDIR}/${MACHODIR};
.	for hdr src in ${MACHO}
		cmp -s ${src} ${DESTDIR}/${MACHODIR}/${hdr} || \
		${INSTALL} ${INSTALL_COPY} -m 444 ${src} ${DESTDIR}/${MACHODIR}/${hdr};
.	endfor

.include <bsd.lib.mk>

.else
NOPROG=
.include <bsd.prog.mk>
.endif
