# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	tests
TESTSDIR=	${TESTSBASE}/sys/cddl/zfs/tests/rootpool
FILESDIR=	${TESTSDIR}

ATF_TESTS_KSH93+=	rootpool_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	rootpool_007_neg.ksh
${PACKAGE}FILES+=	rootpool_002_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	rootpool_001_pos.ksh

.include <bsd.test.mk>
