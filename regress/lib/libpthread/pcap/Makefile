# $OpenBSD: Makefile,v 1.9 2004/12/29 05:09:02 david Exp $

PROG=	pcap

DPADD+=	${LIBPCAP}
LDADD+=	-lpcap

REGRESS_ROOT_TARGETS=	run-regress-${PROG}

run-regress-${PROG}: ${PROG}
	${SUDO} ./${PROG}

.include <bsd.regress.mk>
