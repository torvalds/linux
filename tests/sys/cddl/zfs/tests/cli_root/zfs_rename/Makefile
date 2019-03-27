# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_rename
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_rename_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_rename_002_pos.ksh
${PACKAGE}FILES+=	zfs_rename_006_pos.ksh
${PACKAGE}FILES+=	zfs_rename_012_neg.ksh
${PACKAGE}FILES+=	zfs_rename.cfg
${PACKAGE}FILES+=	zfs_rename_013_pos.ksh
${PACKAGE}FILES+=	zfs_rename_003_pos.ksh
${PACKAGE}FILES+=	zfs_rename_007_pos.ksh
${PACKAGE}FILES+=	zfs_rename_001_pos.ksh
${PACKAGE}FILES+=	zfs_rename_004_neg.ksh
${PACKAGE}FILES+=	zfs_rename_008_pos.ksh
${PACKAGE}FILES+=	zfs_rename_009_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_rename_005_neg.ksh
${PACKAGE}FILES+=	zfs_rename.kshlib
${PACKAGE}FILES+=	zfs_rename_010_neg.ksh
${PACKAGE}FILES+=	zfs_rename_011_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh

.include <bsd.test.mk>
