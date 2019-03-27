# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/rsend
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	rsend_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	rsend_003_pos.ksh
${PACKAGE}FILES+=	rsend_007_pos.ksh
${PACKAGE}FILES+=	rsend_012_pos.ksh
${PACKAGE}FILES+=	rsend_013_pos.ksh
${PACKAGE}FILES+=	rsend_002_pos.ksh
${PACKAGE}FILES+=	rsend_006_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	rsend_011_pos.ksh
${PACKAGE}FILES+=	rsend.kshlib
${PACKAGE}FILES+=	rsend_008_pos.ksh
${PACKAGE}FILES+=	rsend_004_pos.ksh
${PACKAGE}FILES+=	rsend_001_pos.ksh
${PACKAGE}FILES+=	rsend_005_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	rsend_009_pos.ksh
${PACKAGE}FILES+=	rsend.cfg
${PACKAGE}FILES+=	rsend_010_pos.ksh

.include <bsd.test.mk>
