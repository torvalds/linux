# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/utils_test
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	utils_test_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	utils_test_007_pos.ksh
${PACKAGE}FILES+=	utils_test_003_pos.ksh
${PACKAGE}FILES+=	utils_test_006_pos.ksh
${PACKAGE}FILES+=	utils_test_002_pos.ksh
${PACKAGE}FILES+=	utils_test.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	utils_test_004_pos.ksh
${PACKAGE}FILES+=	utils_test_008_pos.ksh
${PACKAGE}FILES+=	utils_test.cfg
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	utils_test_009_pos.ksh
${PACKAGE}FILES+=	utils_test_005_pos.ksh
${PACKAGE}FILES+=	utils_test_001_pos.ksh

.include <bsd.test.mk>
