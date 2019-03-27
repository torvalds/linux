# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_offline
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_offline_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_offline_002_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_offline_001_pos.ksh
${PACKAGE}FILES+=	zpool_offline.cfg

.include <bsd.test.mk>
