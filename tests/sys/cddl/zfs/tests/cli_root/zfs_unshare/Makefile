# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_unshare
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_unshare_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_unshare_003_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_unshare_002_pos.ksh
${PACKAGE}FILES+=	zfs_unshare.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_unshare_005_neg.ksh
${PACKAGE}FILES+=	zfs_unshare_001_pos.ksh
${PACKAGE}FILES+=	zfs_unshare_004_neg.ksh

.include <bsd.test.mk>
