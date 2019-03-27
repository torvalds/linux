# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_destroy
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_destroy_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_destroy_001_pos.ksh
${PACKAGE}FILES+=	zfs_destroy.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_destroy_004_pos.ksh
${PACKAGE}FILES+=	zfs_destroy_005_neg.ksh
${PACKAGE}FILES+=	zfs_destroy_common.kshlib
${PACKAGE}FILES+=	zfs_destroy_002_pos.ksh
${PACKAGE}FILES+=	zfs_destroy_007_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_destroy_003_pos.ksh
${PACKAGE}FILES+=	zfs_destroy_006_neg.ksh

.include <bsd.test.mk>
