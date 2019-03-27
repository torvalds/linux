# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_add
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_add_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_add.cfg
${PACKAGE}FILES+=	zpool_add.kshlib
${PACKAGE}FILES+=	zpool_add_001_pos.ksh
${PACKAGE}FILES+=	zpool_add_002_pos.ksh
${PACKAGE}FILES+=	zpool_add_003_pos.ksh
${PACKAGE}FILES+=	zpool_add_004_pos.ksh
${PACKAGE}FILES+=	zpool_add_005_pos.ksh
${PACKAGE}FILES+=	zpool_add_006_pos.ksh
${PACKAGE}FILES+=	zpool_add_007_neg.ksh
${PACKAGE}FILES+=	zpool_add_008_neg.ksh
${PACKAGE}FILES+=	zpool_add_009_neg.ksh
${PACKAGE}FILES+=	zpool_add_010_pos.ksh

.include <bsd.test.mk>
