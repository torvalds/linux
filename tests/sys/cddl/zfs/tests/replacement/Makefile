# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/replacement
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	replacement_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	replacement_002_pos.ksh
${PACKAGE}FILES+=	replacement_003_pos.ksh
${PACKAGE}FILES+=	replacement_001_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	replacement.kshlib

.include <bsd.test.mk>
