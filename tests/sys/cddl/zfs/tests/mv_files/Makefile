# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/mv_files
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	mv_files_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	mv_files_common.kshlib
${PACKAGE}FILES+=	mv_files_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	mv_files_001_pos.ksh
${PACKAGE}FILES+=	mv_files.cfg

.include <bsd.test.mk>
