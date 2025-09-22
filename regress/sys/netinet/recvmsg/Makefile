#	$OpenBSD: Makefile,v 1.1 2018/07/04 02:16:54 anton Exp $

PROG=		recvmsg
WARNINGS=	yes

REGRESS_TARGETS+=	dgram raw

dgram: ${PROG}
	./${PROG} -4d

raw: ${PROG}
	${SUDO} ./${PROG} -4r

.include <bsd.regress.mk>
