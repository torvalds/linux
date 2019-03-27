# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	tests
TESTSDIR=	${TESTSBASE}/sys/cddl/zfs/tests/reservation
FILESDIR=	${TESTSDIR}

ATF_TESTS_KSH93+=	reservation_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	reservation.cfg
${PACKAGE}FILES+=	reservation_007_pos.ksh
${PACKAGE}FILES+=	reservation_003_pos.ksh
${PACKAGE}FILES+=	reservation_012_pos.ksh
${PACKAGE}FILES+=	reservation_016_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	reservation_013_pos.ksh
${PACKAGE}FILES+=	reservation_017_pos.ksh
${PACKAGE}FILES+=	reservation_006_pos.ksh
${PACKAGE}FILES+=	reservation_002_pos.ksh
${PACKAGE}FILES+=	reservation_011_pos.ksh
${PACKAGE}FILES+=	reservation_015_pos.ksh
${PACKAGE}FILES+=	reservation_004_pos.ksh
${PACKAGE}FILES+=	reservation_008_pos.ksh
${PACKAGE}FILES+=	reservation_009_pos.ksh
${PACKAGE}FILES+=	reservation_005_pos.ksh
${PACKAGE}FILES+=	reservation_001_pos.ksh
${PACKAGE}FILES+=	reservation.kshlib
${PACKAGE}FILES+=	reservation_010_pos.ksh
${PACKAGE}FILES+=	reservation_014_pos.ksh
${PACKAGE}FILES+=	reservation_018_pos.ksh

.include <bsd.test.mk>
