# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/snapshot
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	snapshot_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	rollback_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	snapshot_017_pos.ksh
${PACKAGE}FILES+=	snapshot_013_pos.ksh
${PACKAGE}FILES+=	ctldir_acl.txt
${PACKAGE}FILES+=	snapshot_002_pos.ksh
${PACKAGE}FILES+=	snapshot_006_pos.ksh
${PACKAGE}FILES+=	clone_001_pos.ksh
${PACKAGE}FILES+=	snapshot_003_pos.ksh
${PACKAGE}FILES+=	snapshot_007_pos.ksh
${PACKAGE}FILES+=	snapshot_016_pos.ksh
${PACKAGE}FILES+=	snapshot_012_pos.ksh
${PACKAGE}FILES+=	snapshot_009_pos.ksh
${PACKAGE}FILES+=	snapshot_001_pos.ksh
${PACKAGE}FILES+=	snapshot_005_pos.ksh
${PACKAGE}FILES+=	snapshot.cfg
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	rollback_002_pos.ksh
${PACKAGE}FILES+=	snapshot_014_pos.ksh
${PACKAGE}FILES+=	snapshot_010_pos.ksh
${PACKAGE}FILES+=	snapshot_018_pos.ksh
${PACKAGE}FILES+=	rollback_003_pos.ksh
${PACKAGE}FILES+=	snapshot_019_pos.ksh
${PACKAGE}FILES+=	snapshot_015_pos.ksh
${PACKAGE}FILES+=	snapshot_011_pos.ksh
${PACKAGE}FILES+=	snapshot_004_pos.ksh
${PACKAGE}FILES+=	snapshot_008_pos.ksh
${PACKAGE}FILES+=	snapshot_020_pos.ksh

.include <bsd.test.mk>
