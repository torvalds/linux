# $OpenBSD: Makefile,v 1.3 2018/05/21 10:14:50 bluhm Exp $

PROG=	switch

.for count in 2 3 4 6 8 20 26
REGRESS_TARGETS +=	run-regress-${PROG}-${count}
run-regress-${PROG}-${count}: ${PROG}
	time ./${PROG} -c ${count} | wc -c
.endfor

.include <bsd.regress.mk>
