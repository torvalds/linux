# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cache
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	cache_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cache_005_neg.ksh
${PACKAGE}FILES+=	cache.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	cache_011_pos.ksh
${PACKAGE}FILES+=	cache_010_neg.ksh
${PACKAGE}FILES+=	cache_008_neg.ksh
${PACKAGE}FILES+=	cache_004_neg.ksh
${PACKAGE}FILES+=	cache_009_pos.ksh
${PACKAGE}FILES+=	cache_001_pos.ksh
${PACKAGE}FILES+=	cache_003_pos.ksh
${PACKAGE}FILES+=	cache_007_neg.ksh
${PACKAGE}FILES+=	cache_002_pos.ksh
${PACKAGE}FILES+=	cache_006_pos.ksh
${PACKAGE}FILES+=	cache.cfg
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
