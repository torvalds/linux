# $FreeBSD$

.PATH: ${SRCTOP}/sys/netipsec

KMOD=   tcpmd5
SRCS=   xform_tcp.c opt_inet.h opt_inet6.h opt_ipsec.h

opt_ipsec.h:
	@echo "#define IPSEC_SUPPORT 1" > ${.TARGET}

.include <bsd.kmod.mk>
