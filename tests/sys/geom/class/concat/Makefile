# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/geom/class/${.CURDIR:T}

TAP_TESTS_SH+=	1_test
TAP_TESTS_SH+=	2_test

${PACKAGE}FILES+=		conf.sh

.for t in ${TAP_TESTS_SH}
TEST_METADATA.$t+=	required_user="root"
.endfor

.include <bsd.test.mk>
