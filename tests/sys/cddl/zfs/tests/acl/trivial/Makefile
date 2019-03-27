# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/acl/trivial
FILESDIR=${TESTSDIR}

${PACKAGE}FILES+=	zfs_acl_chmod_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_compress_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cp_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cp_002_neg.ksh
${PACKAGE}FILES+=	zfs_acl_cp_003_neg.ksh
${PACKAGE}FILES+=	zfs_acl_find_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_find_002_neg.ksh
${PACKAGE}FILES+=	zfs_acl_ls_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_ls_002_neg.ksh
${PACKAGE}FILES+=	zfs_acl_mv_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pack_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_003_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_004_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_005_pos.ksh
${PACKAGE}FILES+=	zfs_acl_pax_006_pos.ksh
${PACKAGE}FILES+=	zfs_acl_tar_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_tar_002_neg.ksh

ATF_TESTS_KSH93+=	trivial_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

.include <bsd.test.mk>
