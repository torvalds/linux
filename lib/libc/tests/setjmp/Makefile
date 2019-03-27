# $FreeBSD$

NETBSD_ATF_TESTS_C=	setjmp_test
NETBSD_ATF_TESTS_C+=	threadjmp_test

LIBADD.t_threadjmp+=	pthread

WARNS?=	4

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
