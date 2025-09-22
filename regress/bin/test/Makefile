# $OpenBSD: Makefile,v 1.2 2012/08/21 15:30:48 matthew Exp $

REGRESS_TARGETS=	run_test

run_test:
	test=/bin/test /bin/sh $(.CURDIR)/TEST.sh >/dev/null

.include <bsd.regress.mk>
