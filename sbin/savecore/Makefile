# $FreeBSD$

PACKAGE=runtime
CONFS=	minfree
VAR_CRASH=	/var/crash
VAR_CRASH_MODE=	0750
CONFSDIR=	VAR_CRASH
PROG=	savecore
LIBADD=	xo z
MAN=	savecore.8

.include <src.opts.mk>

.if ${MK_DYNAMICROOT} == "no"
.warning ${PROG} built without libcasper support
.elif ${MK_CASPER} != "no" && !defined(RESCUE)
CFLAGS+=	-DWITH_CASPER
LIBADD+=	casper cap_fileargs cap_syslog
.endif

.include <bsd.prog.mk>
