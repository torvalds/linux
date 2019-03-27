# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_set
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_set_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_set_002_neg.ksh
${PACKAGE}FILES+=	zpool_set_003_neg.ksh
${PACKAGE}FILES+=	zpool_set_001_pos.ksh

.include <bsd.test.mk>
