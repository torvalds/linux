# $OpenBSD: Makefile,v 1.5 2024/02/07 19:26:58 anton Exp $

REGRESS_TARGETS= 	network_statement
REGRESS_ROOT_TARGETS=	${REGRESS_TARGETS}

OSPF6D ?=		/usr/sbin/ospf6d

N1?=	11
N2?=	12

network_statement:
	${SUDO} ksh ${.CURDIR}/$@.sh ${OSPF6D} ${.CURDIR} ${.OBJDIR} \
		${N1} ${N2} pair${N1} pair${N2}

.include <bsd.regress.mk>
