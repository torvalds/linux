/*
 *  ext4_sb.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs_sb.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _EXT4_SB
#define _EXT4_SB

#ifdef __KERNEL__
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>
#endif
#include <linux/rbtree.h>

/*
 * third extended-fs super-block data in memory
 */
struct ext4_sb_info {
	unsigned long s_desc_size;	/* Size of a group descriptor in bytes */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	ext4_group_t s_groups_count;	/* Number of groups in the fs */
	unsigned long s_overhead_last;  /* Last calculated overhead */
	unsigned long s_blocks_last;    /* Last seen block count */
	loff_t s_bitmap_maxbytes;	/* max bytes for bitmap files */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct ext4_super_block * s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head ** s_group_desc;
	unsigned long  s_mount_opt;
	ext4_fsblk_t s_sb_block;
	uid_t s_resuid;
	gid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	u32 s_hash_seed[4];
	int s_def_hash_version;
	struct percpu_counter s_freeblocks_counter;
	struct percpu_counter s_freeinodes_counter;
	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock s_blockgroup_lock;

	/* root of the per fs reservation window tree */
	spinlock_t s_rsv_window_lock;
	struct rb_root s_rsv_window_root;
	struct ext4_reserve_window_node s_rsv_window_head;

	/* Journaling */
	struct inode * s_journal_inode;
	struct journal_s * s_journal;
	struct list_head s_orphan;
	unsigned long s_commit_interval;
	struct block_device *journal_bdev;
#ifdef CONFIG_JBD2_DEBUG
	struct timer_list turn_ro_timer;	/* For turning read-only (crash simulation) */
	wait_queue_head_t ro_wait_queue;	/* For people waiting for the fs to go read-only */
#endif
#ifdef CONFIG_QUOTA
	char *s_qf_names[MAXQUOTAS];		/* Names of quota files with journalled quota */
	int s_jquota_fmt;			/* Format of quota to use */
#endif
	unsigned int s_want_extra_isize; /* New inodes should reserve # bytes */

#ifdef EXTENTS_STATS
	/* ext4 extents stats */
	unsigned long s_ext_min;
	unsigned long s_ext_max;
	unsigned long s_depth_max;
	spinlock_t s_ext_stats_lock;
	unsigned long s_ext_blocks;
	unsigned long s_ext_extents;
#endif

	/* for buddy allocator */
	struct ext4_group_info ***s_group_info;
	struct inode *s_buddy_cache;
	long s_blocks_reserved;
	spinlock_t s_reserve_lock;
	struct list_head s_active_transaction;
	struct list_head s_closed_transaction;
	struct list_head s_committed_transaction;
	spinlock_t s_md_lock;
	tid_t s_last_transaction;
	unsigned short *s_mb_offsets, *s_mb_maxs;

	/* tunables */
	unsigned long s_stripe;
	unsigned long s_mb_stream_request;
	unsigned long s_mb_max_to_scan;
	unsigned long s_mb_min_to_scan;
	unsigned long s_mb_stats;
	unsigned long s_mb_order2_reqs;
	unsigned long s_mb_group_prealloc;
	/* where last allocation was done - for stream allocation */
	unsigned long s_mb_last_group;
	unsigned long s_mb_last_start;

	/* history to debug policy */
	struct ext4_mb_history *s_mb_history;
	int s_mb_history_cur;
	int s_mb_history_max;
	int s_mb_history_num;
	struct proc_dir_entry *s_mb_proc;
	spinlock_t s_mb_history_lock;
	int s_mb_history_filter;

	/* stats for buddy allocator */
	spinlock_t s_mb_pa_lock;
	atomic_t s_bal_reqs;	/* number of reqs with len > 1 */
	atomic_t s_bal_success;	/* we found long enough chunks */
	atomic_t s_bal_allocated;	/* in blocks */
	atomic_t s_bal_ex_scanned;	/* total extents scanned */
	atomic_t s_bal_goals;	/* goal hits */
	atomic_t s_bal_breaks;	/* too long searches */
	atomic_t s_bal_2orders;	/* 2^order hits */
	spinlock_t s_bal_lock;
	unsigned long s_mb_buddies_generated;
	unsigned long long s_mb_generation_time;
	atomic_t s_mb_lost_chunks;
	atomic_t s_mb_preallocated;
	atomic_t s_mb_discarded;

	/* locality groups */
	struct ext4_locality_group *s_locality_groups;
};

#endif	/* _EXT4_SB */
