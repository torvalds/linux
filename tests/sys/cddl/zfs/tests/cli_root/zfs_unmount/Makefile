# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_unmount
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_unmount_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_unmount_003_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_all_001_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_006_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_002_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_007_neg.ksh
${PACKAGE}FILES+=	zfs_unmount.cfg
${PACKAGE}FILES+=	zfs_unmount_004_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_009_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_005_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_001_pos.ksh
${PACKAGE}FILES+=	zfs_unmount_008_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_unmount.kshlib

.include <bsd.test.mk>
