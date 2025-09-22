#	$OpenBSD: Makefile,v 1.2 2004/01/16 19:34:37 miod Exp $

PROG=	infinity

DPADD+=	${LIBM}
LDADD+=	-lm

REGRESS_TARGETS+= add mult neg pumpkin

add: ${PROG}
	./${PROG} -a

mult: ${PROG}
	./${PROG} -m

neg: ${PROG}
	./${PROG} -n

pumpkin: ${PROG}
	./${PROG} -p

.include <bsd.regress.mk>
