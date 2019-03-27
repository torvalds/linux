# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_promote
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_promote_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_promote_006_neg.ksh
${PACKAGE}FILES+=	zfs_promote_003_pos.ksh
${PACKAGE}FILES+=	zfs_promote_common.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_promote_007_neg.ksh
${PACKAGE}FILES+=	zfs_promote.cfg
${PACKAGE}FILES+=	zfs_promote_002_pos.ksh
${PACKAGE}FILES+=	zfs_promote_004_pos.ksh
${PACKAGE}FILES+=	zfs_promote_008_pos.ksh
${PACKAGE}FILES+=	zfs_promote_005_pos.ksh
${PACKAGE}FILES+=	zfs_promote_001_pos.ksh

.include <bsd.test.mk>
