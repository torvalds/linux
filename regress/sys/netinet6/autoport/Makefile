#	$OpenBSD: Makefile,v 1.3 2021/10/05 05:57:58 anton Exp $

REGRESS_TARGETS=	run-regress-autoport
REGRESS_ROOT_TARGETS=	$(REGRESS_TARGETS)

run-regress-autoport:
	${SUDO} /usr/bin/perl ${.CURDIR}/autoport.pl 6 30000 31000 64

.include <bsd.regress.mk>
