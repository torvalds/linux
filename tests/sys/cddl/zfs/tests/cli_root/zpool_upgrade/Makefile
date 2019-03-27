# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_upgrade
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_upgrade_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_upgrade_003_pos.ksh
${PACKAGE}FILES+=	zpool_upgrade_007_pos.ksh
${PACKAGE}FILES+=	zpool_upgrade_006_neg.ksh
${PACKAGE}FILES+=	zpool_upgrade_002_pos.ksh
${PACKAGE}FILES+=	zpool_upgrade.cfg
${PACKAGE}FILES+=	zpool_upgrade_004_pos.ksh
${PACKAGE}FILES+=	zpool_upgrade_008_pos.ksh
${PACKAGE}FILES+=	zpool_upgrade_005_neg.ksh
${PACKAGE}FILES+=	zpool_upgrade_009_neg.ksh
${PACKAGE}FILES+=	zpool_upgrade.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_upgrade_001_pos.ksh

SUBDIR+=	blockfiles

.include <bsd.test.mk>
