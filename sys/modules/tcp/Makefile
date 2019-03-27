#
# $FreeBSD$
#

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

SUBDIR=	\
        ${_tcp_rack} \
	${_tcpmd5} \

.if ${MK_EXTRA_TCP_STACKS} != "no" || defined(ALL_MODULES)
_tcp_rack= 	rack
.endif

.if (${MK_INET_SUPPORT} != "no" || ${MK_INET6_SUPPORT} != "no") || \
	defined(ALL_MODULES)
.if ${MK_IPSEC_SUPPORT} != "no"
_tcpmd5=	tcpmd5
.endif
.endif

.include <bsd.subdir.mk>
