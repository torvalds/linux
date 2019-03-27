# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_export
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_export_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_export.cfg
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_export_001_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_export_004_pos.ksh
${PACKAGE}FILES+=	zpool_export_003_neg.ksh
${PACKAGE}FILES+=	zpool_export_002_pos.ksh

.include <bsd.test.mk>
