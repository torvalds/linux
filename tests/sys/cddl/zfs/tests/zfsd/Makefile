# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zfsd
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfsd_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	hotspare_cleanup.ksh
${PACKAGE}FILES+=	hotspare_setup.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	zfsd.cfg
${PACKAGE}FILES+=	zfsd.kshlib
${PACKAGE}FILES+=	zfsd_autoreplace_001_neg.ksh
${PACKAGE}FILES+=	zfsd_autoreplace_002_pos.ksh
${PACKAGE}FILES+=	zfsd_autoreplace_003_pos.ksh
${PACKAGE}FILES+=	zfsd_degrade_001_pos.ksh
${PACKAGE}FILES+=	zfsd_degrade_002_pos.ksh
${PACKAGE}FILES+=	zfsd_fault_001_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_001_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_002_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_003_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_004_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_005_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_006_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_007_pos.ksh
${PACKAGE}FILES+=	zfsd_hotspare_008_neg.ksh
${PACKAGE}FILES+=	zfsd_import_001_pos.ksh
${PACKAGE}FILES+=	zfsd_replace_001_pos.ksh
${PACKAGE}FILES+=	zfsd_replace_002_pos.ksh
${PACKAGE}FILES+=	zfsd_replace_003_pos.ksh

.include <bsd.test.mk>
