#	$OpenBSD: Makefile,v 1.4 2019/05/12 22:12:16 rob Exp $

REGRESS_TARGETS= run-regress run-regress-int-i

LDFLAGS= -lutil

CLEANFILES= ber_test ber_test.d
CLEANFILES+=ber_test_int_i ber_test_int_i.d

run-regress: ber_test
	${.OBJDIR}/ber_test

run-regress-int-i: ber_test_int_i
	${.OBJDIR}/ber_test_int_i

.include <bsd.regress.mk>
