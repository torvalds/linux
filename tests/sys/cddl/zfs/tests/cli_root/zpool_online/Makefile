# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_online
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_online_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_online.cfg
${PACKAGE}FILES+=	zpool_online_002_neg.ksh
${PACKAGE}FILES+=	zpool_online_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
