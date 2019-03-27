# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/hotplug
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	hotplug_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	hotplug.cfg
${PACKAGE}FILES+=	hotplug.kshlib
${PACKAGE}FILES+=	hotplug_001_pos.ksh
${PACKAGE}FILES+=	hotplug_008_pos.ksh
${PACKAGE}FILES+=	hotplug_011_pos.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
