# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_rollback
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_rollback_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_rollback_003_neg.ksh
${PACKAGE}FILES+=	zfs_rollback_002_pos.ksh
${PACKAGE}FILES+=	zfs_rollback_common.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_rollback_004_neg.ksh
${PACKAGE}FILES+=	zfs_rollback_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_rollback.cfg

.include <bsd.test.mk>
