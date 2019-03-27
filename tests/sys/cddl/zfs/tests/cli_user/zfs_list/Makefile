# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_user/zfs_list
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_list_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_list_006_pos.ksh
${PACKAGE}FILES+=	zfs_list_002_pos.ksh
${PACKAGE}FILES+=	zfs_list_007_pos.ksh
${PACKAGE}FILES+=	zfs_list_003_pos.ksh
${PACKAGE}FILES+=	zfs_list.cfg
${PACKAGE}FILES+=	zfs_list_005_pos.ksh
${PACKAGE}FILES+=	zfs_list_001_pos.ksh
${PACKAGE}FILES+=	zfs_list.kshlib
${PACKAGE}FILES+=	zfs_list_008_neg.ksh
${PACKAGE}FILES+=	zfs_list_004_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
