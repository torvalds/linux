# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/large_files
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	large_files_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	large_files_001_pos.ksh
${PACKAGE}FILES+=	large_files.cfg

.include <bsd.test.mk>
