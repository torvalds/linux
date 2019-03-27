# $FreeBSD$

.include <src.opts.mk>

PACKAGE=		tests

TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_diff
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_diff_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_diff_001_pos.ksh
${PACKAGE}FILES+=	zfs_diff.cfg
${PACKAGE}FILES+=	zfs_diff_001_pos.golden

.include <bsd.test.mk>
