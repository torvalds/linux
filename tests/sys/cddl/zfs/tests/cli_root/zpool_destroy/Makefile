# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_destroy
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_destroy_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh

${PACKAGE}FILES+=	zpool_destroy.cfg
${PACKAGE}FILES+=	zpool_destroy_001_pos.ksh
${PACKAGE}FILES+=	zpool_destroy_002_pos.ksh
${PACKAGE}FILES+=	zpool_destroy_003_neg.ksh
${PACKAGE}FILES+=	zpool_destroy_004_pos.ksh

.include <bsd.test.mk>
