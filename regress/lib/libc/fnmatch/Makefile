#	$OpenBSD: Makefile,v 1.2 2011/09/17 15:12:38 stsp Exp $

PROG=	fnm_test
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

run-regress-${PROG}:
	./${PROG} ${.CURDIR}/${PROG}.in

.include <bsd.regress.mk>
