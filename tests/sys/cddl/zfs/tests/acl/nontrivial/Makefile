# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/acl/nontrivial
FILESDIR=${TESTSDIR}

${PACKAGE}FILES+=	zfs_acl_chmod_001_neg.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_aclmode_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_compact_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_delete_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_inherit_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_inherit_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_inherit_003_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_inherit_004_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_owner_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_rwacl_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_rwx_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_rwx_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_rwx_003_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_rwx_004_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_xattr_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_chmod_xattr_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cp_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cp_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cpio_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_cpio_002_pos.ksh
${PACKAGE}FILES+=	zfs_acl_find_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_ls_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_mv_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_tar_001_pos.ksh
${PACKAGE}FILES+=	zfs_acl_tar_002_pos.ksh

ATF_TESTS_KSH93+=	nontrivial_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

.include <bsd.test.mk>
