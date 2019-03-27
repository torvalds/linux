# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_send
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_send_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_send_003_pos.ksh
${PACKAGE}FILES+=	zfs_send_002_pos.ksh
${PACKAGE}FILES+=	zfs_send.cfg
${PACKAGE}FILES+=	zfs_send_001_pos.ksh
${PACKAGE}FILES+=	zfs_send_004_neg.ksh

.include <bsd.test.mk>
