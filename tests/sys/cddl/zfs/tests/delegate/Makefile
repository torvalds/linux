# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/delegate
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_allow_test
ATF_TESTS_KSH93+=	zfs_unallow_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	delegate.cfg
${PACKAGE}FILES+=	delegate_common.kshlib
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_allow_001_pos.ksh
${PACKAGE}FILES+=	zfs_allow_002_pos.ksh
${PACKAGE}FILES+=	zfs_allow_003_pos.ksh
${PACKAGE}FILES+=	zfs_allow_004_pos.ksh
${PACKAGE}FILES+=	zfs_allow_005_pos.ksh
${PACKAGE}FILES+=	zfs_allow_006_pos.ksh
${PACKAGE}FILES+=	zfs_allow_007_pos.ksh
${PACKAGE}FILES+=	zfs_allow_008_pos.ksh
${PACKAGE}FILES+=	zfs_allow_009_neg.ksh
${PACKAGE}FILES+=	zfs_allow_010_pos.ksh
${PACKAGE}FILES+=	zfs_allow_011_neg.ksh
${PACKAGE}FILES+=	zfs_allow_012_neg.ksh
${PACKAGE}FILES+=	zfs_unallow_001_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_002_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_003_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_004_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_005_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_006_pos.ksh
${PACKAGE}FILES+=	zfs_unallow_007_neg.ksh
${PACKAGE}FILES+=	zfs_unallow_008_neg.ksh

.include <bsd.test.mk>
