# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/snapused
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	snapused_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	snapused_005_pos.ksh
${PACKAGE}FILES+=	snapused_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	snapused_004_pos.ksh
${PACKAGE}FILES+=	snapused.kshlib
${PACKAGE}FILES+=	snapused.cfg
${PACKAGE}FILES+=	snapused_002_pos.ksh
${PACKAGE}FILES+=	snapused_003_pos.ksh

.include <bsd.test.mk>
