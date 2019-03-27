# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/iscsi
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	iscsi_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	iscsi_004_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	iscsi_005_pos.ksh
${PACKAGE}FILES+=	iscsi_001_pos.ksh
${PACKAGE}FILES+=	iscsi_common.kshlib
${PACKAGE}FILES+=	iscsi_002_neg.ksh
${PACKAGE}FILES+=	iscsi_006_neg.ksh
${PACKAGE}FILES+=	iscsi_003_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	iscsi.cfg

.include <bsd.test.mk>
