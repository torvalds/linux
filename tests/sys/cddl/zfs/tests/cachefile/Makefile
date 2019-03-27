# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cachefile
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	cachefile_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cachefile_002_pos.ksh
${PACKAGE}FILES+=	cachefile_003_pos.ksh
${PACKAGE}FILES+=	cachefile_001_pos.ksh
${PACKAGE}FILES+=	cachefile.cfg
${PACKAGE}FILES+=	cachefile.kshlib
${PACKAGE}FILES+=	cachefile_004_pos.ksh

.include <bsd.test.mk>
