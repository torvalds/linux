# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_copies
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_copies_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_copies.cfg
${PACKAGE}FILES+=	zfs_copies_001_pos.ksh
${PACKAGE}FILES+=	zfs_copies_004_neg.ksh
${PACKAGE}FILES+=	zfs_copies_005_neg.ksh
${PACKAGE}FILES+=	zfs_copies.kshlib
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs_copies_006_pos.ksh
${PACKAGE}FILES+=	zfs_copies_002_pos.ksh
${PACKAGE}FILES+=	zfs_copies_003_pos.ksh

.include <bsd.test.mk>
