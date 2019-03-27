# $FreeBSD$

SHLIB_NAME=	libomp.so

OMPSRC=		${SRCTOP}/contrib/openmp/runtime/src
ITTSRC=		${OMPSRC}/thirdparty/ittnotify
.PATH:		${OMPSRC}
.PATH:		${ITTSRC}

SRCS+=		ittnotify_static.c
SRCS+=		kmp_affinity.cpp
SRCS+=		kmp_alloc.cpp
SRCS+=		kmp_atomic.cpp
SRCS+=		kmp_barrier.cpp
SRCS+=		kmp_cancel.cpp
SRCS+=		kmp_csupport.cpp
SRCS+=		kmp_debug.cpp
SRCS+=		kmp_dispatch.cpp
SRCS+=		kmp_environment.cpp
SRCS+=		kmp_error.cpp
SRCS+=		kmp_ftn_cdecl.cpp
SRCS+=		kmp_ftn_extra.cpp
SRCS+=		kmp_global.cpp
SRCS+=		kmp_gsupport.cpp
SRCS+=		kmp_i18n.cpp
SRCS+=		kmp_io.cpp
SRCS+=		kmp_itt.cpp
SRCS+=		kmp_lock.cpp
SRCS+=		kmp_runtime.cpp
SRCS+=		kmp_sched.cpp
SRCS+=		kmp_settings.cpp
SRCS+=		kmp_str.cpp
SRCS+=		kmp_taskdeps.cpp
SRCS+=		kmp_tasking.cpp
SRCS+=		kmp_taskq.cpp
SRCS+=		kmp_threadprivate.cpp
SRCS+=		kmp_utility.cpp
SRCS+=		kmp_version.cpp
SRCS+=		kmp_wait_release.cpp
SRCS+=		ompt-general.cpp
SRCS+=		z_Linux_asm.S
SRCS+=		z_Linux_util.cpp
INCS+=		omp.h

WARNS?=		1

CFLAGS+=	-D__STDC_CONSTANT_MACROS
CFLAGS+=	-D__STDC_FORMAT_MACROS
CFLAGS+=	-D__STDC_LIMIT_MACROS
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${OMPSRC}
CFLAGS+=	-I${ITTSRC}
CFLAGS+=	-ffunction-sections
CFLAGS+=	-fdata-sections
CXXFLAGS+=	-fvisibility-inlines-hidden
CXXFLAGS+=	-std=c++11
CXXFLAGS+=	-fno-exceptions
CXXFLAGS+=	-fno-rtti

LDFLAGS+=	-Wl,--warn-shared-textrel
LDFLAGS+=	-Wl,--gc-sections
LDFLAGS+=	-Wl,-z,noexecstack
LDFLAGS+=	-Wl,-fini=__kmp_internal_end_fini
LDFLAGS+=	-Wl,-soname,libomp.so

VERSION_MAP=	${OMPSRC}/exports_so.txt

LIBADD+=	pthread
LIBADD+=	m

.include <bsd.lib.mk>
