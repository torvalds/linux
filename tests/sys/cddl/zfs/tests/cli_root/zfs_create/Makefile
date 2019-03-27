# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_create
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_create_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	properties.kshlib
${PACKAGE}FILES+=	zfs_create_011_pos.ksh
${PACKAGE}FILES+=	zfs_create_010_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_create_009_neg.ksh
${PACKAGE}FILES+=	zfs_create_004_pos.ksh
${PACKAGE}FILES+=	zfs_create_common.kshlib
${PACKAGE}FILES+=	zfs_create_008_neg.ksh
${PACKAGE}FILES+=	zfs_create_005_pos.ksh
${PACKAGE}FILES+=	zfs_create_001_pos.ksh
${PACKAGE}FILES+=	zfs_create_007_pos.ksh
${PACKAGE}FILES+=	zfs_create_003_pos.ksh
${PACKAGE}FILES+=	zfs_create_012_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_create_013_pos.ksh
${PACKAGE}FILES+=	zfs_create.cfg
${PACKAGE}FILES+=	zfs_create_006_pos.ksh
${PACKAGE}FILES+=	zfs_create_002_pos.ksh

.include <bsd.test.mk>
