# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/refquota
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	refquota_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	refquota_001_pos.ksh
${PACKAGE}FILES+=	refquota_005_pos.ksh
${PACKAGE}FILES+=	refquota.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	refquota_004_pos.ksh
${PACKAGE}FILES+=	refquota_002_pos.ksh
${PACKAGE}FILES+=	refquota_003_pos.ksh
${PACKAGE}FILES+=	refquota_006_neg.ksh

.include <bsd.test.mk>
