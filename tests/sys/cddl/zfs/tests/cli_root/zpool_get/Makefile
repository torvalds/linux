# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_get
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_get_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_get_002_pos.ksh
${PACKAGE}FILES+=	zpool_get_003_pos.ksh
${PACKAGE}FILES+=	zpool_get.cfg
${PACKAGE}FILES+=	zpool_get_001_pos.ksh
${PACKAGE}FILES+=	zpool_get_004_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh

.include <bsd.test.mk>
