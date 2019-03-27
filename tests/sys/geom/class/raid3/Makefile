# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/geom/class/${.CURDIR:T}

TAP_TESTS_SH+=	1_test
TAP_TESTS_SH+=	2_test
TAP_TESTS_SH+=	3_test
TAP_TESTS_SH+=	4_test
TAP_TESTS_SH+=	5_test
TAP_TESTS_SH+=	6_test
TAP_TESTS_SH+=	7_test
TAP_TESTS_SH+=	8_test
TAP_TESTS_SH+=	9_test
TAP_TESTS_SH+=	10_test
TAP_TESTS_SH+=	11_test
TAP_TESTS_SH+=	12_test

${PACKAGE}FILES+=		conf.sh

.for t in ${TAP_TESTS_SH}
TEST_METADATA.$t+=	required_user="root"
.endfor

.include <bsd.test.mk>
