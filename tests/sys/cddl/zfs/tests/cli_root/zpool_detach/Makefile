# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_detach
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_detach_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_detach.cfg
${PACKAGE}FILES+=	zpool_detach_001_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
