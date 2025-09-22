#	$OpenBSD: Makefile,v 1.1 2018/07/22 06:37:46 anton Exp $

PROG=		fchown
WARNINGS=	yes

REGRESS_TARGETS+=	clone-device

clone-device: ${PROG}
	${SUDO} ./${PROG}

.include <bsd.regress.mk>
