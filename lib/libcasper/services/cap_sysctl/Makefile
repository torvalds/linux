# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	1
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_sysctl

SRCS=	cap_sysctl.c
.endif

INCS=	cap_sysctl.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

MAN+=	cap_sysctl.3

MLINKS+=cap_sysctl.3 libcap_sysctl.3
MLINKS+=cap_sysctl.3 cap_sysctlbyname.3

.include <bsd.lib.mk>
