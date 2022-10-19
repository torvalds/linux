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

enum {
	BTRFS_FS_CLOSING_START,
	BTRFS_FS_CLOSING_DONE,
	BTRFS_FS_LOG_RECOVERING,
	BTRFS_FS_OPEN,
	BTRFS_FS_QUOTA_ENABLED,
	BTRFS_FS_UPDATE_UUID_TREE_GEN,
	BTRFS_FS_CREATING_FREE_SPACE_TREE,
	BTRFS_FS_BTREE_ERR,
	BTRFS_FS_LOG1_ERR,
	BTRFS_FS_LOG2_ERR,
	BTRFS_FS_QUOTA_OVERRIDE,
	/* Used to record internally whether fs has been frozen */
	BTRFS_FS_FROZEN,
	/*
	 * Indicate that balance has been set up from the ioctl and is in the
	 * main phase. The fs_info::balance_ctl is initialized.
	 */
	BTRFS_FS_BALANCE_RUNNING,

	/*
	 * Indicate that relocation of a chunk has started, it's set per chunk
	 * and is toggled between chunks.
	 */
	BTRFS_FS_RELOC_RUNNING,

	/* Indicate that the cleaner thread is awake and doing something. */
	BTRFS_FS_CLEANER_RUNNING,

	/*
	 * The checksumming has an optimized version and is considered fast,
	 * so we don't need to offload checksums to workqueues.
	 */
	BTRFS_FS_CSUM_IMPL_FAST,

	/* Indicate that the discard workqueue can service discards. */
	BTRFS_FS_DISCARD_RUNNING,

	/* Indicate that we need to cleanup space cache v1 */
	BTRFS_FS_CLEANUP_SPACE_CACHE_V1,

	/* Indicate that we can't trust the free space tree for caching yet */
	BTRFS_FS_FREE_SPACE_TREE_UNTRUSTED,

	/* Indicate whether there are any tree modification log users */
	BTRFS_FS_TREE_MOD_LOG_USERS,

	/* Indicate that we want the transaction kthread to commit right now. */
	BTRFS_FS_COMMIT_TRANS,

	/* Indicate we have half completed snapshot deletions pending. */
	BTRFS_FS_UNFINISHED_DROPS,

	/* Indicate we have to finish a zone to do next allocation. */
	BTRFS_FS_NEED_ZONE_FINISH,

	/* Indicate that we want to commit the transaction. */
	BTRFS_FS_NEED_TRANS_COMMIT,

#if BITS_PER_LONG == 32
	/* Indicate if we have error/warn message printed on 32bit systems */
	BTRFS_FS_32BIT_ERROR,
	BTRFS_FS_32BIT_WARN,
#endif
};

/*
 * Flags for mount options.
 *
 * Note: don't forget to add new options to btrfs_show_options()
 */
enum {
	BTRFS_MOUNT_NODATASUM			= (1UL << 0),
	BTRFS_MOUNT_NODATACOW			= (1UL << 1),
	BTRFS_MOUNT_NOBARRIER			= (1UL << 2),
	BTRFS_MOUNT_SSD				= (1UL << 3),
	BTRFS_MOUNT_DEGRADED			= (1UL << 4),
	BTRFS_MOUNT_COMPRESS			= (1UL << 5),
	BTRFS_MOUNT_NOTREELOG   		= (1UL << 6),
	BTRFS_MOUNT_FLUSHONCOMMIT		= (1UL << 7),
	BTRFS_MOUNT_SSD_SPREAD			= (1UL << 8),
	BTRFS_MOUNT_NOSSD			= (1UL << 9),
	BTRFS_MOUNT_DISCARD_SYNC		= (1UL << 10),
	BTRFS_MOUNT_FORCE_COMPRESS      	= (1UL << 11),
	BTRFS_MOUNT_SPACE_CACHE			= (1UL << 12),
	BTRFS_MOUNT_CLEAR_CACHE			= (1UL << 13),
	BTRFS_MOUNT_USER_SUBVOL_RM_ALLOWED	= (1UL << 14),
	BTRFS_MOUNT_ENOSPC_DEBUG		= (1UL << 15),
	BTRFS_MOUNT_AUTO_DEFRAG			= (1UL << 16),
	BTRFS_MOUNT_USEBACKUPROOT		= (1UL << 17),
	BTRFS_MOUNT_SKIP_BALANCE		= (1UL << 18),
	BTRFS_MOUNT_CHECK_INTEGRITY		= (1UL << 19),
	BTRFS_MOUNT_CHECK_INTEGRITY_DATA	= (1UL << 20),
	BTRFS_MOUNT_PANIC_ON_FATAL_ERROR	= (1UL << 21),
	BTRFS_MOUNT_RESCAN_UUID_TREE		= (1UL << 22),
	BTRFS_MOUNT_FRAGMENT_DATA		= (1UL << 23),
	BTRFS_MOUNT_FRAGMENT_METADATA		= (1UL << 24),
	BTRFS_MOUNT_FREE_SPACE_TREE		= (1UL << 25),
	BTRFS_MOUNT_NOLOGREPLAY			= (1UL << 26),
	BTRFS_MOUNT_REF_VERIFY			= (1UL << 27),
	BTRFS_MOUNT_DISCARD_ASYNC		= (1UL << 28),
	BTRFS_MOUNT_IGNOREBADROOTS		= (1UL << 29),
	BTRFS_MOUNT_IGNOREDATACSUMS		= (1UL << 30),
	BTRFS_MOUNT_NODISCARD			= (1UL << 31),
};

/*
 * Compat flags that we support.  If any incompat flags are set other than the
 * ones specified below then we will fail to mount
 */
#define BTRFS_FEATURE_COMPAT_SUPP		0ULL
#define BTRFS_FEATURE_COMPAT_SAFE_SET		0ULL
#define BTRFS_FEATURE_COMPAT_SAFE_CLEAR		0ULL

#define BTRFS_FEATURE_COMPAT_RO_SUPP			\
	(BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE |	\
	 BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE_VALID | \
	 BTRFS_FEATURE_COMPAT_RO_VERITY |		\
	 BTRFS_FEATURE_COMPAT_RO_BLOCK_GROUP_TREE)

#define BTRFS_FEATURE_COMPAT_RO_SAFE_SET	0ULL
#define BTRFS_FEATURE_COMPAT_RO_SAFE_CLEAR	0ULL

#ifdef CONFIG_BTRFS_DEBUG
/*
 * Extent tree v2 supported only with CONFIG_BTRFS_DEBUG
 */
#define BTRFS_FEATURE_INCOMPAT_SUPP			\
	(BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF |		\
	 BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL |	\
	 BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS |		\
	 BTRFS_FEATURE_INCOMPAT_BIG_METADATA |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD |		\
	 BTRFS_FEATURE_INCOMPAT_RAID56 |		\
	 BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF |		\
	 BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA |	\
	 BTRFS_FEATURE_INCOMPAT_NO_HOLES	|	\
	 BTRFS_FEATURE_INCOMPAT_METADATA_UUID	|	\
	 BTRFS_FEATURE_INCOMPAT_RAID1C34	|	\
	 BTRFS_FEATURE_INCOMPAT_ZONED		|	\
	 BTRFS_FEATURE_INCOMPAT_EXTENT_TREE_V2)
#else
#define BTRFS_FEATURE_INCOMPAT_SUPP			\
	(BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF |		\
	 BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL |	\
	 BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS |		\
	 BTRFS_FEATURE_INCOMPAT_BIG_METADATA |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD |		\
	 BTRFS_FEATURE_INCOMPAT_RAID56 |		\
	 BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF |		\
	 BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA |	\
	 BTRFS_FEATURE_INCOMPAT_NO_HOLES	|	\
	 BTRFS_FEATURE_INCOMPAT_METADATA_UUID	|	\
	 BTRFS_FEATURE_INCOMPAT_RAID1C34	|	\
	 BTRFS_FEATURE_INCOMPAT_ZONED)
#endif

#define BTRFS_FEATURE_INCOMPAT_SAFE_SET			\
	(BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF)
#define BTRFS_FEATURE_INCOMPAT_SAFE_CLEAR		0ULL

#define BTRFS_DEFAULT_COMMIT_INTERVAL	(30)
#define BTRFS_DEFAULT_MAX_INLINE	(2048)

/* Compatibility and incompatibility defines */
void __btrfs_set_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			     const char *name);
void __btrfs_clear_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			       const char *name);
void __btrfs_set_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
			      const char *name);
void __btrfs_clear_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
				const char *name);

#define __btrfs_fs_incompat(fs_info, flags)				\
	(!!(btrfs_super_incompat_flags((fs_info)->super_copy) & (flags)))

#define __btrfs_fs_compat_ro(fs_info, flags)				\
	(!!(btrfs_super_compat_ro_flags((fs_info)->super_copy) & (flags)))

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

#define btrfs_clear_opt(o, opt)		((o) &= ~BTRFS_MOUNT_##opt)
#define btrfs_set_opt(o, opt)		((o) |= BTRFS_MOUNT_##opt)
#define btrfs_raw_test_opt(o, opt)	((o) & BTRFS_MOUNT_##opt)
#define btrfs_test_opt(fs_info, opt)	((fs_info)->mount_opt & \
					 BTRFS_MOUNT_##opt)

#define btrfs_set_and_info(fs_info, opt, fmt, args...)			\
do {									\
	if (!btrfs_test_opt(fs_info, opt))				\
		btrfs_info(fs_info, fmt, ##args);			\
	btrfs_set_opt(fs_info->mount_opt, opt);				\
} while (0)

#define btrfs_clear_and_info(fs_info, opt, fmt, args...)		\
do {									\
	if (btrfs_test_opt(fs_info, opt))				\
		btrfs_info(fs_info, fmt, ##args);			\
	btrfs_clear_opt(fs_info->mount_opt, opt);			\
} while (0)

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

static inline void btrfs_wake_unfinished_drop(struct btrfs_fs_info *fs_info)
{
	clear_and_wake_up_bit(BTRFS_FS_UNFINISHED_DROPS, &fs_info->flags);
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
