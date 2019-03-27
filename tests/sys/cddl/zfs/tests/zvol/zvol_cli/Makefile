# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zvol/zvol_cli
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zvol_cli_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zvol_cli_003_neg.ksh
${PACKAGE}FILES+=	zvol_cli_002_pos.ksh
${PACKAGE}FILES+=	zvol_cli.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zvol_cli_001_pos.ksh

.include <bsd.test.mk>
