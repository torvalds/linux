# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zpool_import
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zpool_import_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zpool_import_006_pos.ksh
${PACKAGE}FILES+=	zpool_import_002_pos.ksh
${PACKAGE}FILES+=	zpool_import_missing_003_pos.ksh
${PACKAGE}FILES+=	zpool_import_all_001_pos.ksh
${PACKAGE}FILES+=	zpool_import.cfg
${PACKAGE}FILES+=	zpool_import_corrupt_001_pos.ksh
${PACKAGE}FILES+=	zpool_import_destroyed_001_neg.ksh
${PACKAGE}FILES+=	zpool_import_destroyed_002_neg.ksh
${PACKAGE}FILES+=	zpool_import_012_pos.ksh
${PACKAGE}FILES+=	zpool_import_013_neg.ksh
${PACKAGE}FILES+=	zpool_import_007_pos.ksh
${PACKAGE}FILES+=	zpool_import_003_pos.ksh
${PACKAGE}FILES+=	zpool_import_rename_001_pos.ksh
${PACKAGE}FILES+=	zpool_import_missing_002_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zpool_import_010_pos.ksh
${PACKAGE}FILES+=	zpool_import_014_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_import_011_neg.ksh
${PACKAGE}FILES+=	zpool_import_missing_004_pos.ksh
${PACKAGE}FILES+=	zpool_import_missing_005_pos.ksh
${PACKAGE}FILES+=	zpool_import_005_pos.ksh
${PACKAGE}FILES+=	zpool_import_009_neg.ksh
${PACKAGE}FILES+=	zpool_import_missing_001_pos.ksh
${PACKAGE}FILES+=	zpool_import_008_pos.ksh
${PACKAGE}FILES+=	zpool_import_004_pos.ksh
${PACKAGE}FILES+=	zpool_import.kshlib

SUBDIR+=	blockfiles

.include <bsd.test.mk>
