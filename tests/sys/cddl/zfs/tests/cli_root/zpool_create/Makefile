# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_create
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_create_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_create_003_pos.ksh
${PACKAGE}FILES+=	zpool_create_020_pos.ksh
${PACKAGE}FILES+=	zpool_create_017_neg.ksh
${PACKAGE}FILES+=	zpool_create_013_neg.ksh
${PACKAGE}FILES+=	zpool_create_016_pos.ksh
${PACKAGE}FILES+=	zpool_create_012_neg.ksh
${PACKAGE}FILES+=	zpool_create_006_pos.ksh
${PACKAGE}FILES+=	zpool_create_002_pos.ksh
${PACKAGE}FILES+=	zpool_create_021_pos.ksh
${PACKAGE}FILES+=	zpool_create_007_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_create_014_neg.ksh
${PACKAGE}FILES+=	zpool_create_010_neg.ksh
${PACKAGE}FILES+=	zpool_create_019_pos.ksh
${PACKAGE}FILES+=	zpool_create_008_pos.ksh
${PACKAGE}FILES+=	zpool_create_004_pos.ksh
${PACKAGE}FILES+=	zpool_create_009_neg.ksh
${PACKAGE}FILES+=	zpool_create_022_pos.ksh
${PACKAGE}FILES+=	zpool_create.cfg
${PACKAGE}FILES+=	zpool_create_005_pos.ksh
${PACKAGE}FILES+=	zpool_create_001_pos.ksh
${PACKAGE}FILES+=	zpool_create_023_neg.ksh
${PACKAGE}FILES+=	zpool_create.kshlib
${PACKAGE}FILES+=	zpool_create_015_neg.ksh
${PACKAGE}FILES+=	zpool_create_011_neg.ksh
${PACKAGE}FILES+=	zpool_create_018_pos.ksh

.include <bsd.test.mk>
