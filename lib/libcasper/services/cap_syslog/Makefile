# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	1
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_syslog

SRCS=	cap_syslog.c
.endif

INCS=	cap_syslog.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

MAN+=	cap_syslog.3

MLINKS+= cap_syslog.3 libcap_syslog.3
MLINKS+= cap_syslog.3 cap_vsyslog.3
MLINKS+= cap_syslog.3 cap_openlog.3
MLINKS+= cap_syslog.3 cap_closelog.3
MLINKS+= cap_syslog.3 cap_setlogmask.3

.include <bsd.lib.mk>
