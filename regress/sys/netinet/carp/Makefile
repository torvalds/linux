#	$OpenBSD: Makefile,v 1.2 2023/10/11 18:07:15 anton Exp $

REGRESS_TARGETS=	carp_1 carp_2 carp_3 carp_4
REGRESS_ROOT_TARGETS=	carp_1 carp_2 carp_3 carp_4

N1=			11
N2=			12
RDOMAINS=		${N1} ${N2}
IFACE_NUMS=		11 12

#TESTOPTS=		-v

carp_1 carp_2 carp_3 carp_4:
	${SUDO} ksh ${.CURDIR}/$@.sh -R "${RDOMAINS}" -I "${IFACE_NUMS}" \
	    ${TESTOPTS}

.include <bsd.regress.mk>
