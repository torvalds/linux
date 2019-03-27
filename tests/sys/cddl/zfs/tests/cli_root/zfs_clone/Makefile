# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_clone
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_clone_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_clone_005_pos.ksh
${PACKAGE}FILES+=	zfs_clone_008_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_clone_004_pos.ksh
${PACKAGE}FILES+=	zfs_clone_001_neg.ksh
${PACKAGE}FILES+=	zfs_clone_009_neg.ksh
${PACKAGE}FILES+=	zfs_clone_006_pos.ksh
${PACKAGE}FILES+=	zfs_clone_002_pos.ksh
${PACKAGE}FILES+=	zfs_clone.cfg
${PACKAGE}FILES+=	zfs_clone_007_pos.ksh
${PACKAGE}FILES+=	zfs_clone_003_pos.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
