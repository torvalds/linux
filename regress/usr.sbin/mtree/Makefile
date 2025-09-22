# $OpenBSD: Makefile,v 1.1 2008/06/13 21:33:00 espie Exp $

REGRESS_TARGETS=t1


t1:
	mtree -e -f ${.CURDIR}/spec1 -p ${.CURDIR}

.include <bsd.regress.mk>
