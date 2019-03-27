# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/devices
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	devices_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	devices_common.kshlib
${PACKAGE}FILES+=	devices_002_neg.ksh
${PACKAGE}FILES+=	devices_test.sh
${PACKAGE}FILES+=	devices_003_pos.ksh
${PACKAGE}FILES+=	devices.cfg
${PACKAGE}FILES+=	devices_001_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
