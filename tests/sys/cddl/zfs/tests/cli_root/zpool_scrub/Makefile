# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_scrub
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_scrub_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_scrub_001_neg.ksh
${PACKAGE}FILES+=	zpool_scrub_004_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_scrub_005_pos.ksh
${PACKAGE}FILES+=	zpool_scrub.cfg
${PACKAGE}FILES+=	zpool_scrub_003_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_scrub_002_pos.ksh

.include <bsd.test.mk>
