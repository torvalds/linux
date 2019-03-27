# $FreeBSD$

.include <bsd.own.mk>

NO_WERROR=
WARNS?=	2

CFLAGS.h_raw+=	-fstack-protector-all -Wstack-protector
.if ${COMPILER_TYPE} == "clang"
CFLAGS.h_raw+=	-fsanitize=bounds
.elif ${COMPILER_TYPE} == "gcc"
CFLAGS.h_raw+=	--param ssp-buffer-size=1
LDADD+=	-lssp
.endif

NETBSD_ATF_TESTS_SH=	ssp_test

BINDIR=		${TESTSDIR}

PROGS=		h_fgets
PROGS+=		h_gets
PROGS+=		h_getcwd
PROGS+=		h_memcpy
PROGS+=		h_memmove
PROGS+=		h_memset
# This testcase doesn't run properly when not compiled with -fsantize=bounds
# with clang, which is currently contingent on a compiler_rt update
#
# XXX: the h_raw/h_read testcases don't cause a SIGABRT with in-tree gcc right
# now on amd64 when it trips the stack bounds specified in t_ssp.sh . This
# probably needs to be fixed as it's currently hardcoded.
#
# sanitizer is not tested or supported for ARM right now. sbruno
.if ${COMPILER_TYPE} == "clang" && !defined(_SKIP_BUILD) && \
    (!defined(_RECURSING_PROGS) || ${PROG} == "h_raw")
.if !defined(_CLANG_RESOURCE_DIR)
_CLANG_RESOURCE_DIR!=	${CC:N${CCACHE_BIN}} -print-resource-dir
.export _CLANG_RESOURCE_DIR
.endif
_libclang_rt_arch=	${MACHINE_ARCH:S/amd64/x86_64/:C/hf$//:S/mipsn32/mips64/}
_libclang_rt_ubsan=	${_CLANG_RESOURCE_DIR}/lib/freebsd/libclang_rt.ubsan_standalone-${_libclang_rt_arch}.a
.if exists(${_libclang_rt_ubsan})
PROGS+=		h_raw
.endif
.endif
PROGS+=		h_read
PROGS+=		h_readlink
PROGS+=		h_snprintf
PROGS+=		h_sprintf
PROGS+=		h_stpcpy
PROGS+=		h_stpncpy
PROGS+=		h_strcat
PROGS+=		h_strcpy
PROGS+=		h_strncat
PROGS+=		h_strncpy
PROGS+=		h_vsnprintf
PROGS+=		h_vsprintf

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
