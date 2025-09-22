#	$OpenBSD: Makefile,v 1.2 2009/06/09 18:16:40 phessler Exp $

PROG=		getcaptest

REGRESS_TARGETS=shortcap

shortcap: getcaptest
	./getcaptest ${.CURDIR}/shortcap.in

.include <bsd.regress.mk>
