#	$OpenBSD: Makefile,v 1.1 2018/07/09 09:03:29 mpi Exp $

LDADD= -lusbhid
DPADD= ${LIBUSBHID}
WARNINGS= Yes
CFLAGS+= -Werror


PROG= parsetest
REGRESS_TARGETS+= run-regress-${PROG}-hex
REGRESS_TARGETS+= run-regress-${PROG}-dec
REGRESS_TARGETS+= run-regress-${PROG}-static

.include <bsd.regress.mk>

# hextest is commented in runparse.sh
run-regress-${PROG}-hex: ${PROG}
	sh ${.CURDIR}/runparse.sh hex "100002 100012"

run-regress-${PROG}-dec: ${PROG}
	sh ${.CURDIR}/runparse.sh dec "90002 A0002 810002"

run-regress-${PROG}-static: ${PROG}
	sh ${.CURDIR}/runparse.sh static "B0002 D0002 820012"

