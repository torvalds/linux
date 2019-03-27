# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/migration
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	migration_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	migration_011_pos.ksh
${PACKAGE}FILES+=	migration_008_pos.ksh
${PACKAGE}FILES+=	migration_004_pos.ksh
${PACKAGE}FILES+=	migration.cfg
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	migration_005_pos.ksh
${PACKAGE}FILES+=	migration_001_pos.ksh
${PACKAGE}FILES+=	migration_009_pos.ksh
${PACKAGE}FILES+=	migration_010_pos.ksh
${PACKAGE}FILES+=	migration_007_pos.ksh
${PACKAGE}FILES+=	migration_003_pos.ksh
${PACKAGE}FILES+=	migration_012_pos.ksh
${PACKAGE}FILES+=	migration.kshlib
${PACKAGE}FILES+=	migration_006_pos.ksh
${PACKAGE}FILES+=	migration_002_pos.ksh

.include <bsd.test.mk>
