# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	1
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_grp

SRCS=	cap_grp.c
.endif

INCS=	cap_grp.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

MAN+=	cap_grp.3

MLINKS+=cap_grp.3 libcap_grp.3
MLINKS+=cap_grp.3 cap_getgrent.3
MLINKS+=cap_grp.3 cap_getgrnam.3
MLINKS+=cap_grp.3 cap_getgrgid.3
MLINKS+=cap_grp.3 cap_getgrent_r.3
MLINKS+=cap_grp.3 cap_getgrnam_r.3
MLINKS+=cap_grp.3 cap_getgrgid_r.3
MLINKS+=cap_grp.3 cap_setgroupent.3
MLINKS+=cap_grp.3 cap_setgrent.3
MLINKS+=cap_grp.3 cap_endgrent.3
MLINKS+=cap_grp.3 cap_grp_limit_cmds.3
MLINKS+=cap_grp.3 cap_grp_limit_fields.3
MLINKS+=cap_grp.3 cap_grp_limit_groups.3

.include <bsd.lib.mk>
