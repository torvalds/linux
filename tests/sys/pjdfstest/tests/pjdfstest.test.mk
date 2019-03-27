# $FreeBSD$

PJDFSTEST_SRCDIR=	${.CURDIR:H:H:H:H:H}/contrib/pjdfstest

.PATH: ${PJDFSTEST_SRCDIR}/tests/${.CURDIR:T}

TESTSDIR?=	${TESTSBASE}/sys/pjdfstest/${.CURDIR:T}

.for s in ${TAP_TESTS_SH}
TAP_TESTS_SH_SRC_$s=	$s.t
TEST_METADATA.$s+=	required_user="root"
.endfor

.include <bsd.test.mk>
