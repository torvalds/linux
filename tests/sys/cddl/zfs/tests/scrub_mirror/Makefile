# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/scrub_mirror
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	scrub_mirror_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	scrub_mirror_004_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	scrub_mirror_001_pos.ksh
${PACKAGE}FILES+=	scrub_mirror_003_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	default.cfg
${PACKAGE}FILES+=	scrub_mirror_002_pos.ksh
${PACKAGE}FILES+=	scrub_mirror_common.kshlib

.include <bsd.test.mk>
