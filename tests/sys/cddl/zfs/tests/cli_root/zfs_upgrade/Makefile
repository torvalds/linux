# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_upgrade
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_upgrade_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_upgrade.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_upgrade_004_pos.ksh
${PACKAGE}FILES+=	zfs_upgrade_001_pos.ksh
${PACKAGE}FILES+=	zfs_upgrade_005_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_upgrade.kshlib
${PACKAGE}FILES+=	zfs_upgrade_006_neg.ksh
${PACKAGE}FILES+=	zfs_upgrade_003_pos.ksh
${PACKAGE}FILES+=	zfs_upgrade_007_neg.ksh
${PACKAGE}FILES+=	zfs_upgrade_002_pos.ksh

.include <bsd.test.mk>
