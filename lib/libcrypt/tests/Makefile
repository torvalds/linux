# $FreeBSD$

ATF_TESTS_C+= crypt_tests

NETBSD_ATF_TESTS_C+= crypt_test

WARNS?=		6
CFLAGS+= -I${.CURDIR:H}
LIBADD=	crypt

.include <netbsd-tests.test.mk>

.include <bsd.test.mk>
