# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/txg_integrity
FILESDIR=${TESTSDIR}
BINDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	txg_integrity_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	txg_integrity_001_pos.ksh
${PACKAGE}FILES+=	fsync_integrity_001_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	txg_integrity.cfg
PROGS+=	txg_integrity
PROGS+=	fsync_integrity
LIBADD+=	pthread

SRCS.fsync_integrity=	fsync_integrity.c
SRCS.txg_integrity=	txg_integrity.c

.include <bsd.test.mk>
