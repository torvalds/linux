# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_expand
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_expand_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_expand_003_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_expand_002_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_expand.cfg
${PACKAGE}FILES+=	zpool_expand_001_pos.ksh

.include <bsd.test.mk>
