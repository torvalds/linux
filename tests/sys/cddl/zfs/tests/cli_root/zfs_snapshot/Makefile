# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_snapshot
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_snapshot_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_snapshot_005_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot_001_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot_004_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_snapshot_002_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot_006_pos.ksh
${PACKAGE}FILES+=	zfs_snapshot_007_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot_003_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot.cfg

.include <bsd.test.mk>
