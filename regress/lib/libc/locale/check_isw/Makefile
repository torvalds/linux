#	$OpenBSD: Makefile,v 1.2 2017/07/27 15:08:37 bluhm Exp $

PROG =	check_isw

run-regress-${PROG}: ${PROG}
	./${PROG} >/dev/null

.include <bsd.regress.mk>
