# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

KMOD=	ng_checksum
SRCS= 	ng_checksum.c opt_inet.h opt_inet6.h

.include <bsd.kmod.mk>
