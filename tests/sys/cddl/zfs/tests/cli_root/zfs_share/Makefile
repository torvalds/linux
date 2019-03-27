# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_share
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_share_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_share_003_pos.ksh
${PACKAGE}FILES+=	zfs_share_006_pos.ksh
${PACKAGE}FILES+=	zfs_share_002_pos.ksh
${PACKAGE}FILES+=	zfs_share_007_neg.ksh
${PACKAGE}FILES+=	zfs_share.cfg
${PACKAGE}FILES+=	zfs_share_004_pos.ksh
${PACKAGE}FILES+=	zfs_share_009_neg.ksh
${PACKAGE}FILES+=	zfs_share_010_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_share_009_pos.ksh
${PACKAGE}FILES+=	zfs_share_005_pos.ksh
${PACKAGE}FILES+=	zfs_share_001_pos.ksh
${PACKAGE}FILES+=	zfs_share_008_neg.ksh

.include <bsd.test.mk>
