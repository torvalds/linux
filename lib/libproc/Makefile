# $FreeBSD$

.include <src.opts.mk>

PACKAGE=lib${LIB}
LIB=	proc

SRCS=	crc32.c			\
	proc_bkpt.c		\
	proc_create.c		\
	proc_regs.c		\
	proc_sym.c		\
	proc_rtld.c		\
	proc_util.c

INCS=	libproc.h

CFLAGS+=	-I${.CURDIR}

.if ${MK_CXX} == "no"
CFLAGS+=	-DNO_CXA_DEMANGLE
.elif ${MK_LIBCPLUSPLUS} != "no"
LIBADD+=		cxxrt
.else
LIBADD+=	supcplusplus
.endif

LIBADD+=	elf procstat rtld_db util

.if ${MK_CDDL} != "no"
LIBADD+=	ctf
IGNORE_PRAGMA=	YES
CFLAGS+=	-I${SRCTOP}/cddl/contrib/opensolaris/lib/libctf/common \
		-I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common \
		-I${SRCTOP}/sys/cddl/compat/opensolaris
.else
CFLAGS+=	-DNO_CTF
.endif

SHLIB_MAJOR=	5

MAN=

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.lib.mk>
