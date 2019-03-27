# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_mount
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_mount_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_mount_004_pos.ksh
${PACKAGE}FILES+=	zfs_mount_008_pos.ksh
${PACKAGE}FILES+=	zfs_mount_009_neg.ksh
${PACKAGE}FILES+=	zfs_mount_010_neg.ksh
${PACKAGE}FILES+=	zfs_mount_011_neg.ksh
${PACKAGE}FILES+=	zfs_mount_005_pos.ksh
${PACKAGE}FILES+=	zfs_mount_001_pos.ksh
${PACKAGE}FILES+=	zfs_mount.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_mount.kshlib
${PACKAGE}FILES+=	zfs_mount_007_pos.ksh
${PACKAGE}FILES+=	zfs_mount_003_pos.ksh
${PACKAGE}FILES+=	zfs_mount_all_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_mount_006_pos.ksh
${PACKAGE}FILES+=	zfs_mount_002_pos.ksh

.include <bsd.test.mk>
