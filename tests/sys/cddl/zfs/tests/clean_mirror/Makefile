# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/clean_mirror
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	clean_mirror_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	clean_mirror_001_pos.ksh
${PACKAGE}FILES+=	clean_mirror_002_pos.ksh
${PACKAGE}FILES+=	clean_mirror_003_pos.ksh
${PACKAGE}FILES+=	clean_mirror_004_pos.ksh
${PACKAGE}FILES+=	clean_mirror_common.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	default.cfg
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
