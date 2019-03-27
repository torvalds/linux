# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/quota
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	quota_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	quota_003_pos.ksh
${PACKAGE}FILES+=	quota_006_neg.ksh
${PACKAGE}FILES+=	quota_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	quota.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	quota_004_pos.ksh
${PACKAGE}FILES+=	quota_005_pos.ksh
${PACKAGE}FILES+=	quota_001_pos.ksh
${PACKAGE}FILES+=	quota.cfg

.include <bsd.test.mk>
