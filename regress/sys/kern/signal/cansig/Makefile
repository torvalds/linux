#	$OpenBSD: Makefile,v 1.1 2008/06/10 18:04:31 hshoexer Exp $ 

PROG=	cansig
CFLAGS+=-Wall

t-cansig: ${PROG}
	${SUDO} ./${PROG}

REGRESS_TARGETS=t-cansig
REGRESS_ROOT_TARGETS=${REGRESS_TARGETS}

.include <bsd.regress.mk>
