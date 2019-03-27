# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zinject
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zinject_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zinject.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zinject_004_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zinject_001_pos.ksh
${PACKAGE}FILES+=	zinject.kshlib
${PACKAGE}FILES+=	zinject_003_pos.ksh
${PACKAGE}FILES+=	zinject_002_pos.ksh

.include <bsd.test.mk>
