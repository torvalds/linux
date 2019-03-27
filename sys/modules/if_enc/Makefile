# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

.PATH: ${SYSDIR}/net

KMOD=	if_enc
SRCS=	if_enc.c opt_inet.h opt_inet6.h

.include <bsd.kmod.mk>
