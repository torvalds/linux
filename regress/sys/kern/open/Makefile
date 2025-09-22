#	$OpenBSD: Makefile,v 1.1 2018/07/30 17:27:37 anton Exp $

PROG=		open
WARNINGS=	yes

REGRESS_TARGETS+=	clone-device

clone-device: ${PROG}
	${SUDO} ./${PROG}

.include <bsd.regress.mk>
