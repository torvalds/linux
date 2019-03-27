# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_001_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool.cfg
${PACKAGE}FILES+=	zpool_003_pos.ksh
${PACKAGE}FILES+=	zpool_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
