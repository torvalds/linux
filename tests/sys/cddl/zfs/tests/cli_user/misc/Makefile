# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/cli_user/misc
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	misc_test
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	zfs_rename_001_neg.ksh
${PACKAGE}FILES+=	zpool_remove_001_neg.ksh
${PACKAGE}FILES+=	zfs_destroy_001_neg.ksh
${PACKAGE}FILES+=	zpool_import_001_neg.ksh
${PACKAGE}FILES+=	zfs_unshare_001_neg.ksh
${PACKAGE}FILES+=	zpool_export_001_neg.ksh
${PACKAGE}FILES+=	zfs_set_001_neg.ksh
${PACKAGE}FILES+=	zpool_upgrade_001_neg.ksh
${PACKAGE}FILES+=	zpool_replace_001_neg.ksh
${PACKAGE}FILES+=	misc.cfg
${PACKAGE}FILES+=	zfs_receive_001_neg.ksh
${PACKAGE}FILES+=	zpool_online_001_neg.ksh
${PACKAGE}FILES+=	zpool_history_001_neg.ksh
${PACKAGE}FILES+=	zpool_create_001_neg.ksh
${PACKAGE}FILES+=	zpool_create_002_pos.ksh
${PACKAGE}FILES+=	zfs_promote_001_neg.ksh
${PACKAGE}FILES+=	zfs_get_001_neg.ksh
${PACKAGE}FILES+=	zfs_share_001_neg.ksh
${PACKAGE}FILES+=	zfs_rollback_001_neg.ksh
${PACKAGE}FILES+=	zfs_unallow_001_neg.ksh
${PACKAGE}FILES+=	zfs_send_001_neg.ksh
${PACKAGE}FILES+=	zpool_clear_001_neg.ksh
${PACKAGE}FILES+=	zpool_add_001_neg.ksh
${PACKAGE}FILES+=	zpool_add_002_pos.ksh
${PACKAGE}FILES+=	zpool_import_002_neg.ksh
${PACKAGE}FILES+=	zpool_set_001_neg.ksh
${PACKAGE}FILES+=	zfs_inherit_001_neg.ksh
${PACKAGE}FILES+=	zfs_unmount_001_neg.ksh
${PACKAGE}FILES+=	zpool_destroy_001_neg.ksh
${PACKAGE}FILES+=	zpool_detach_001_neg.ksh
${PACKAGE}FILES+=	zfs_create_001_neg.ksh
${PACKAGE}FILES+=	zpool_attach_001_neg.ksh
${PACKAGE}FILES+=	zpool_001_neg.ksh
${PACKAGE}FILES+=	zdb_001_neg.ksh
${PACKAGE}FILES+=	zfs_001_neg.ksh
${PACKAGE}FILES+=	zfs_upgrade_001_neg.ksh
${PACKAGE}FILES+=	zpool_scrub_001_neg.ksh
${PACKAGE}FILES+=	zfs_mount_001_neg.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	zpool_get_001_neg.ksh
${PACKAGE}FILES+=	zfs_allow_001_neg.ksh
${PACKAGE}FILES+=	zpool_offline_001_neg.ksh
${PACKAGE}FILES+=	zfs_clone_001_neg.ksh
${PACKAGE}FILES+=	zpool_status_001_neg.ksh
${PACKAGE}FILES+=	zfs_snapshot_001_neg.ksh
${PACKAGE}FILES+=	setup.ksh

.include <bsd.test.mk>
