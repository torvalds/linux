# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zvol_thrash
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zvol_thrash_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zvol_thrash.cfg
${PACKAGE}FILES+=	zvol_thrash_001_pos.ksh

.include <bsd.test.mk>
