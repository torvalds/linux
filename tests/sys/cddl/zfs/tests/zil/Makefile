# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zil
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zil_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zil_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zil.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zil_001_pos.ksh
${PACKAGE}FILES+=	zil.kshlib

.include <bsd.test.mk>
