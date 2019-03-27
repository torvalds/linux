# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zvol/zvol_misc
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zvol_misc_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zvol_misc.cfg
${PACKAGE}FILES+=	zvol_misc_006_pos.ksh
${PACKAGE}FILES+=	zvol_misc_002_pos.ksh
${PACKAGE}FILES+=	zvol_misc_003_neg.ksh
${PACKAGE}FILES+=	zvol_misc_007_pos.ksh
${PACKAGE}FILES+=	zvol_misc_009_pos.ksh
${PACKAGE}FILES+=	zvol_misc_004_pos.ksh
${PACKAGE}FILES+=	zvol_misc_008_pos.ksh
${PACKAGE}FILES+=	zvol_misc_001_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zvol_misc_005_neg.ksh

.include <bsd.test.mk>
