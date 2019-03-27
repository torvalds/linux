# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_inherit
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_inherit_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_inherit_002_neg.ksh
${PACKAGE}FILES+=	zfs_inherit_003_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_inherit_001_neg.ksh
${PACKAGE}FILES+=	zfs_inherit.cfg

.include <bsd.test.mk>
