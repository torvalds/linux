# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/userquota
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	userquota_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	groupspace_001_pos.ksh
${PACKAGE}FILES+=	userquota_common.kshlib
${PACKAGE}FILES+=	userquota_010_pos.ksh
${PACKAGE}FILES+=	userquota_001_pos.ksh
${PACKAGE}FILES+=	userquota_009_pos.ksh
${PACKAGE}FILES+=	userquota_008_pos.ksh
${PACKAGE}FILES+=	userquota_004_pos.ksh
${PACKAGE}FILES+=	userquota_005_neg.ksh
${PACKAGE}FILES+=	userspace_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	userquota_011_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	userquota_002_pos.ksh
${PACKAGE}FILES+=	userquota_006_pos.ksh
${PACKAGE}FILES+=	userquota_012_neg.ksh
${PACKAGE}FILES+=	groupspace_002_pos.ksh
${PACKAGE}FILES+=	userquota_003_pos.ksh
${PACKAGE}FILES+=	userquota_007_pos.ksh
${PACKAGE}FILES+=	userquota.cfg
${PACKAGE}FILES+=	userspace_002_pos.ksh

.include <bsd.test.mk>
