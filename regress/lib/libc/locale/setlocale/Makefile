#	$OpenBSD: Makefile,v 1.2 2017/07/27 15:08:37 bluhm Exp $

PROG =	setlocale

run-regress-${PROG}: ${PROG}
	env -i LC_ALL=fr_FR.UTF-8 ./${PROG}

.include <bsd.regress.mk>
