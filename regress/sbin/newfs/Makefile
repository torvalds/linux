#	$OpenBSD: Makefile,v 1.3 2019/02/12 21:47:19 bluhm Exp $
#
# Regress tests for newfs

REGRESS_TARGETS =	run-regress-checknewfs

run-regress-checknewfs:
	${SUDO} sh ${.CURDIR}/checknewfs

.include <bsd.regress.mk>
