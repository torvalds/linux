#	$OpenBSD: Makefile,v 1.1 2018/07/04 02:20:46 anton Exp $

PROG=		recvmsg
WARNINGS=	yes

CLEANFILES+=	recvmsg.c

recvmsg.c: ${.CURDIR}/../../netinet/recvmsg/recvmsg.c
	cp ${.CURDIR}/../../netinet/recvmsg/recvmsg.c .

REGRESS_TARGETS+=	dgram raw

dgram: ${PROG}
	./${PROG} -6d

raw: ${PROG}
	${SUDO} ./${PROG} -6r

.include <bsd.regress.mk>
