# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/history
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	history_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	history_005_neg.ksh
${PACKAGE}FILES+=	history_004_pos.ksh
${PACKAGE}FILES+=	history_008_pos.ksh
${PACKAGE}FILES+=	i386.orig_history.txt
${PACKAGE}FILES+=	sparc.orig_history.txt
${PACKAGE}FILES+=	history_common.kshlib
${PACKAGE}FILES+=	history.cfg
${PACKAGE}FILES+=	sparc.migratedpool.DAT.Z
${PACKAGE}FILES+=	history_010_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfs-pool-v4.dat.Z
${PACKAGE}FILES+=	history_009_pos.ksh
${PACKAGE}FILES+=	history_001_pos.ksh
${PACKAGE}FILES+=	history_006_neg.ksh
${PACKAGE}FILES+=	i386.migratedpool.DAT.Z
${PACKAGE}FILES+=	history_003_pos.ksh
${PACKAGE}FILES+=	history_007_pos.ksh
${PACKAGE}FILES+=	history_002_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh

.include <bsd.test.mk>
