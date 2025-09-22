# $OpenBSD: Makefile,v 1.1 2016/07/05 20:01:47 schwarze Exp $

REGRESS_TARGETS=cat_be cat_se

cat_be:
	cat -be ${.CURDIR}/cat_be.in | cmp -s ${.CURDIR}/cat_be.out /dev/stdin

cat_se:
	cat -se ${.CURDIR}/cat_se.in | cmp -s ${.CURDIR}/cat_se.out /dev/stdin

.include <bsd.regress.mk>
