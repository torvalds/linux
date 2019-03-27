# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_root/zfs_set
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	zfs_set_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_set_common.kshlib
${PACKAGE}FILES+=	onoffs_001_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	mountpoint_001_pos.ksh
${PACKAGE}FILES+=	share_mount_001_neg.ksh
${PACKAGE}FILES+=	user_property_004_pos.ksh
${PACKAGE}FILES+=	canmount_002_pos.ksh
${PACKAGE}FILES+=	readonly_001_pos.ksh
${PACKAGE}FILES+=	zfs_set_001_neg.ksh
${PACKAGE}FILES+=	user_property_001_pos.ksh
${PACKAGE}FILES+=	snapdir_001_pos.ksh
${PACKAGE}FILES+=	canmount_003_pos.ksh
${PACKAGE}FILES+=	reservation_001_neg.ksh
${PACKAGE}FILES+=	compression_001_pos.ksh
${PACKAGE}FILES+=	cache_001_pos.ksh
${PACKAGE}FILES+=	zfs_set_003_neg.ksh
${PACKAGE}FILES+=	canmount_001_pos.ksh
${PACKAGE}FILES+=	property_alias_001_pos.ksh
${PACKAGE}FILES+=	version_001_neg.ksh
${PACKAGE}FILES+=	zfs_set.cfg
${PACKAGE}FILES+=	cache_002_neg.ksh
${PACKAGE}FILES+=	mountpoint_002_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	checksum_001_pos.ksh
${PACKAGE}FILES+=	mountpoint_003_pos.ksh
${PACKAGE}FILES+=	user_property_002_pos.ksh
${PACKAGE}FILES+=	zfs_set_002_neg.ksh
${PACKAGE}FILES+=	user_property_003_neg.ksh
${PACKAGE}FILES+=	ro_props_001_pos.ksh
${PACKAGE}FILES+=	canmount_004_pos.ksh

.include <bsd.test.mk>
