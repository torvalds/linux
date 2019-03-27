# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/hotspare
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	hotspare_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	hotspare_create_001_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	hotspare_add_004_neg.ksh
${PACKAGE}FILES+=	hotspare_scrub_002_pos.ksh
${PACKAGE}FILES+=	hotspare_detach_002_pos.ksh
${PACKAGE}FILES+=	hotspare_remove_001_pos.ksh
${PACKAGE}FILES+=	hotspare_add_001_pos.ksh
${PACKAGE}FILES+=	hotspare_import_001_pos.ksh
${PACKAGE}FILES+=	hotspare_replace_002_neg.ksh
${PACKAGE}FILES+=	hotspare_clone_002_pos.ksh
${PACKAGE}FILES+=	hotspare_export_001_neg.ksh
${PACKAGE}FILES+=	hotspare_snapshot_001_pos.ksh
${PACKAGE}FILES+=	hotspare_remove_004_pos.ksh
${PACKAGE}FILES+=	hotspare_detach_003_pos.ksh
${PACKAGE}FILES+=	hotspare_onoffline_004_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	hotspare_replace_001_neg.ksh
${PACKAGE}FILES+=	hotspare_scrub_001_pos.ksh
${PACKAGE}FILES+=	hotspare_detach_001_pos.ksh
${PACKAGE}FILES+=	hotspare_add_003_neg.ksh
${PACKAGE}FILES+=	hotspare_remove_003_neg.ksh
${PACKAGE}FILES+=	hotspare_add_002_pos.ksh
${PACKAGE}FILES+=	hotspare_detach_004_pos.ksh
${PACKAGE}FILES+=	hotspare_snapshot_002_pos.ksh
${PACKAGE}FILES+=	hotspare.kshlib
${PACKAGE}FILES+=	hotspare_detach_005_neg.ksh
${PACKAGE}FILES+=	hotspare_remove_002_neg.ksh
${PACKAGE}FILES+=	hotspare_onoffline_003_neg.ksh
${PACKAGE}FILES+=	hotspare_shared_001_pos.ksh
${PACKAGE}FILES+=	hotspare.cfg
${PACKAGE}FILES+=	hotspare_clone_001_pos.ksh

.include <bsd.test.mk>
