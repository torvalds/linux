# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_property
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_property_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_set_property_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_property.cfg
${PACKAGE}FILES+=	cleanup.ksh

.include <bsd.test.mk>
