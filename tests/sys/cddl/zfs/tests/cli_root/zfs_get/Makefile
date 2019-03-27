# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_get
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_get_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_get_002_pos.ksh
${PACKAGE}FILES+=	zfs_get_007_neg.ksh
${PACKAGE}FILES+=	zfs_get_003_pos.ksh
${PACKAGE}FILES+=	zfs_get_list_d.kshlib
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_get_006_neg.ksh
${PACKAGE}FILES+=	zfs_get_009_pos.ksh
${PACKAGE}FILES+=	zfs_get_001_pos.ksh
${PACKAGE}FILES+=	zfs_get_010_neg.ksh
${PACKAGE}FILES+=	zfs_get.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_get_004_pos.ksh
${PACKAGE}FILES+=	zfs_get_008_pos.ksh
${PACKAGE}FILES+=	zfs_get_common.kshlib
${PACKAGE}FILES+=	zfs_get_005_neg.ksh

.include <bsd.test.mk>
