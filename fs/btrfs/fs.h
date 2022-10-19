/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_FS_H
#define BTRFS_FS_H

/*
 * Runtime (in-memory) states of filesystem
 */
enum {
	/* Global indicator of serious filesystem errors */
	BTRFS_FS_STATE_ERROR,
	/*
	 * Filesystem is being remounted, allow to skip some operations, like
	 * defrag
	 */
	BTRFS_FS_STATE_REMOUNTING,
	/* Filesystem in RO mode */
	BTRFS_FS_STATE_RO,
	/* Track if a transaction abort has been reported on this filesystem */
	BTRFS_FS_STATE_TRANS_ABORTED,
	/*
	 * Bio operations should be blocked on this filesystem because a source
	 * or target device is being destroyed as part of a device replace
	 */
	BTRFS_FS_STATE_DEV_REPLACING,
	/* The btrfs_fs_info created for self-tests */
	BTRFS_FS_STATE_DUMMY_FS_INFO,

	BTRFS_FS_STATE_NO_CSUMS,

	/* Indicates there was an error cleaning up a log tree. */
	BTRFS_FS_STATE_LOG_CLEANUP_ERROR,

	BTRFS_FS_STATE_COUNT
};

/* Compatibility and incompatibility defines */
void __btrfs_set_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			     const char *name);
void __btrfs_clear_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			       const char *name);
void __btrfs_set_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
			      const char *name);
void __btrfs_clear_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
				const char *name);

#define btrfs_set_fs_incompat(__fs_info, opt)				\
	__btrfs_set_fs_incompat((__fs_info), BTRFS_FEATURE_INCOMPAT_##opt, #opt)

#define btrfs_clear_fs_incompat(__fs_info, opt)				\
	__btrfs_clear_fs_incompat((__fs_info), BTRFS_FEATURE_INCOMPAT_##opt, #opt)

#define btrfs_fs_incompat(fs_info, opt)					\
	__btrfs_fs_incompat((fs_info), BTRFS_FEATURE_INCOMPAT_##opt)

#define btrfs_set_fs_compat_ro(__fs_info, opt)				\
	__btrfs_set_fs_compat_ro((__fs_info), BTRFS_FEATURE_COMPAT_RO_##opt, #opt)

#define btrfs_clear_fs_compat_ro(__fs_info, opt)			\
	__btrfs_clear_fs_compat_ro((__fs_info), BTRFS_FEATURE_COMPAT_RO_##opt, #opt)

#define btrfs_fs_compat_ro(fs_info, opt)				\
	__btrfs_fs_compat_ro((fs_info), BTRFS_FEATURE_COMPAT_RO_##opt)

static inline bool __btrfs_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag)
{
	struct btrfs_super_block *disk_super;
	disk_super = fs_info->super_copy;
	return !!(btrfs_super_incompat_flags(disk_super) & flag);
}

static inline int __btrfs_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag)
{
	struct btrfs_super_block *disk_super;
	disk_super = fs_info->super_copy;
	return !!(btrfs_super_compat_ro_flags(disk_super) & flag);
}

static inline int btrfs_fs_closing(struct btrfs_fs_info *fs_info)
{
	/* Do it this way so we only ever do one test_bit in the normal case. */
	if (test_bit(BTRFS_FS_CLOSING_START, &fs_info->flags)) {
		if (test_bit(BTRFS_FS_CLOSING_DONE, &fs_info->flags))
			return 2;
		return 1;
	}
	return 0;
}

/*
 * If we remount the fs to be R/O or umount the fs, the cleaner needn't do
 * anything except sleeping. This function is used to check the status of
 * the fs.
 * We check for BTRFS_FS_STATE_RO to avoid races with a concurrent remount,
 * since setting and checking for SB_RDONLY in the superblock's flags is not
 * atomic.
 */
static inline int btrfs_need_cleaner_sleep(struct btrfs_fs_info *fs_info)
{
	return test_bit(BTRFS_FS_STATE_RO, &fs_info->fs_state) ||
		btrfs_fs_closing(fs_info);
}

static inline void btrfs_set_sb_rdonly(struct super_block *sb)
{
	sb->s_flags |= SB_RDONLY;
	set_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

static inline void btrfs_clear_sb_rdonly(struct super_block *sb)
{
	sb->s_flags &= ~SB_RDONLY;
	clear_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

#define BTRFS_FS_ERROR(fs_info)	(unlikely(test_bit(BTRFS_FS_STATE_ERROR, \
						   &(fs_info)->fs_state)))
#define BTRFS_FS_LOG_CLEANUP_ERROR(fs_info)				\
	(unlikely(test_bit(BTRFS_FS_STATE_LOG_CLEANUP_ERROR,		\
			   &(fs_info)->fs_state)))

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
static inline int btrfs_is_testing(struct btrfs_fs_info *fs_info)
{
	return test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state);
}
#else
static inline int btrfs_is_testing(struct btrfs_fs_info *fs_info)
{
	return 0;
}
#endif

#endif
