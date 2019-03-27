# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	1
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_pwd

SRCS=	cap_pwd.c
.endif

INCS=	cap_pwd.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

MAN+=	cap_pwd.3

MLINKS+=cap_pwd.3 libcap_pwd.3
MLINKS+=cap_pwd.3 cap_getpwent.3
MLINKS+=cap_pwd.3 cap_getpwnam.3
MLINKS+=cap_pwd.3 cap_getpwuid.3
MLINKS+=cap_pwd.3 cap_getpwent_r.3
MLINKS+=cap_pwd.3 cap_getpwnam_r.3
MLINKS+=cap_pwd.3 cap_getpwuid_r.3
MLINKS+=cap_pwd.3 cap_setpassent.3
MLINKS+=cap_pwd.3 cap_setpwent.3
MLINKS+=cap_pwd.3 cap_endpwent.3
MLINKS+=cap_pwd.3 cap_pwd_limit_cmds.3
MLINKS+=cap_pwd.3 cap_pwd_limit_fields.3
MLINKS+=cap_pwd.3 cap_pwd_limit_users.3

.include <bsd.lib.mk>
