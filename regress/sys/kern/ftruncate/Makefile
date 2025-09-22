#	$OpenBSD: Makefile,v 1.1 2018/07/08 02:14:54 anton Exp $

PROG=		ftruncate
WARNINGS=	yes

REGRESS_TARGETS+=	clone-device

clone-device: ${PROG}
	${SUDO} ./${PROG}

.include <bsd.regress.mk>
