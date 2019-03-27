# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/mount
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	mount_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	vars.cfg
${PACKAGE}FILES+=	mounttest.ksh
${PACKAGE}FILES+=	mount_test.sh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
