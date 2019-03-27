# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/atime
FILESDIR=${TESTSDIR}

${PACKAGE}FILES+=	atime.cfg
${PACKAGE}FILES+=	atime_001_pos.ksh
${PACKAGE}FILES+=	atime_002_neg.ksh
${PACKAGE}FILES+=	atime_common.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh

ATF_TESTS_KSH93+=	atime_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

.include <bsd.test.mk>
