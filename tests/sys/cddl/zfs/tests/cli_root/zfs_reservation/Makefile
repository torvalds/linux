# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_reservation
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_reservation_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_reservation.cfg
${PACKAGE}FILES+=	zfs_reservation_001_pos.ksh
${PACKAGE}FILES+=	zfs_reservation_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh

.include <bsd.test.mk>
