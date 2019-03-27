# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_receive
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_receive_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_receive_004_neg.ksh
${PACKAGE}FILES+=	zfs_receive_001_pos.ksh
${PACKAGE}FILES+=	zfs_receive_009_neg.ksh
${PACKAGE}FILES+=	zfs_receive_005_neg.ksh
${PACKAGE}FILES+=	zfs_receive_008_pos.ksh
${PACKAGE}FILES+=	zfs_receive.cfg
${PACKAGE}FILES+=	zfs_receive_007_neg.ksh
${PACKAGE}FILES+=	zfs_receive_002_pos.ksh
${PACKAGE}FILES+=	zfs_receive_006_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zfs_receive_003_pos.ksh

.include <bsd.test.mk>
