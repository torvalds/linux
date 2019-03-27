# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	lib${LIB}
LIB=		compiler_rt
NO_PIC=
WARNS?=		2

CFLAGS+=	${PICFLAG}
CFLAGS+=	-fvisibility=hidden
CFLAGS+=	-DVISIBILITY_HIDDEN
CFLAGS+=	-I${SRCTOP}/contrib/libcxxrt

.if ${COMPILER_TYPE} == "clang"
CWARNFLAGS.gcc_personality_v0.c+= -Wno-typedef-redefinition
.endif

# gcc has incompatible internal declarations for __divtc3 and __multc3, but has
# no option to silence its warning, so make warnings non-fatal.
NO_WERROR.gcc=

.include "Makefile.inc"

.if ${MK_INSTALLLIB} != "no"
SYMLINKS+=	libcompiler_rt.a ${LIBDIR}/libgcc.a
.endif
.if ${MK_PROFILE} != "no"
SYMLINKS+=	libcompiler_rt_p.a ${LIBDIR}/libgcc_p.a
.endif

.include <bsd.lib.mk>
