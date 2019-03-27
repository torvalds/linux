# $FreeBSD$

.PATH: ${SRCTOP}/sys/net ${SRCTOP}/sys/netipsec

KMOD=	ipsec
SRCS=	if_ipsec.c ipsec.c ipsec_input.c ipsec_mbuf.c ipsec_mod.c \
	ipsec_output.c xform_ah.c xform_esp.c xform_ipcomp.c \
	opt_inet.h opt_inet6.h opt_ipsec.h opt_sctp.h 
SRCS.INET=	udpencap.c

opt_ipsec.h:
	@echo "#define IPSEC_SUPPORT 1" > ${.TARGET}

.include <bsd.kmod.mk>
