# $FreeBSD$

SYSDIR?=	${SRCTOP}/sys

KMOD=		dtraceall
SRCS=		dtraceall.c opt_nfs.h

CFLAGS+=	-I${SYSDIR}

.include <bsd.kmod.mk>

CFLAGS+=	-include ${SYSDIR}/cddl/compat/opensolaris/sys/debug_compat.h
