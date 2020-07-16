/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/f2fs/gc.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#define GC_THREAD_MIN_WB_PAGES		1	/*
						 * a threshold to determine
						 * whether IO subsystem is idle
						 * or not
						 */
#define DEF_GC_THREAD_URGENT_SLEEP_TIME	500	/* 500 ms */
#define DEF_GC_THREAD_MIN_SLEEP_TIME	30000	/* milliseconds */
#define DEF_GC_THREAD_MAX_SLEEP_TIME	60000
#define DEF_GC_THREAD_NOGC_SLEEP_TIME	300000	/* wait 5 min */
#define LIMIT_INVALID_BLOCK	40 /* percentage over total user space */
#define LIMIT_FREE_BLOCK	40 /* percentage over invalid + free space */

#define DEF_GC_FAILED_PINNED_FILES	2048

/* Search max. number of dirty segments to select a victim segment */
#define DEF_MAX_VICTIM_SEARCH 4096 /* covers 8GB */

struct f2fs_gc_kthread {
	struct task_struct *f2fs_gc_task;
	wait_queue_head_t gc_wait_queue_head;

	/* for gc sleep time */
	unsigned int urgent_sleep_time;
	unsigned int min_sleep_time;
	unsigned int max_sleep_time;
	unsigned int no_gc_sleep_time;

	/* for changing gc mode */
	unsigned int gc_wake;
};

struct gc_inode_list {
	struct list_head ilist;
	struct radix_tree_root iroot;
};

/*
 * inline functions
 */

/*
 * On a Zoned device zone-capacity can be less than zone-size and if
 * zone-capacity is not aligned to f2fs segment size(2MB), then the segment
 * starting just before zone-capacity has some blocks spanning across the
 * zone-capacity, these blocks are not usable.
 * Such spanning segments can be in free list so calculate the sum of usable
 * blocks in currently free segments including normal and spanning segments.
 */
static inline block_t free_segs_blk_count_zoned(struct f2fs_sb_info *sbi)
{
	block_t free_seg_blks = 0;
	struct free_segmap_info *free_i = FREE_I(sbi);
	int j;

	spin_lock(&free_i->segmap_lock);
	for (j = 0; j < MAIN_SEGS(sbi); j++)
		if (!test_bit(j, free_i->free_segmap))
			free_seg_blks += f2fs_usable_blks_in_seg(sbi, j);
	spin_unlock(&free_i->segmap_lock);

	return free_seg_blks;
}

static inline block_t free_segs_blk_count(struct f2fs_sb_info *sbi)
{
	if (f2fs_sb_has_blkzoned(sbi))
		return free_segs_blk_count_zoned(sbi);

	return free_segments(sbi) << sbi->log_blocks_per_seg;
}

static inline block_t free_user_blocks(struct f2fs_sb_info *sbi)
{
	block_t free_blks, ovp_blks;

	free_blks = free_segs_blk_count(sbi);
	ovp_blks = overprovision_segments(sbi) << sbi->log_blocks_per_seg;

	if (free_blks < ovp_blks)
		return 0;

	return free_blks - ovp_blks;
}

static inline block_t limit_invalid_user_blocks(struct f2fs_sb_info *sbi)
{
	return (long)(sbi->user_block_count * LIMIT_INVALID_BLOCK) / 100;
}

static inline block_t limit_free_user_blocks(struct f2fs_sb_info *sbi)
{
	block_t reclaimable_user_blocks = sbi->user_block_count -
		written_block_count(sbi);
	return (long)(reclaimable_user_blocks * LIMIT_FREE_BLOCK) / 100;
}

static inline void increase_sleep_time(struct f2fs_gc_kthread *gc_th,
							unsigned int *wait)
{
	unsigned int min_time = gc_th->min_sleep_time;
	unsigned int max_time = gc_th->max_sleep_time;

	if (*wait == gc_th->no_gc_sleep_time)
		return;

	if ((long long)*wait + (long long)min_time > (long long)max_time)
		*wait = max_time;
	else
		*wait += min_time;
}

static inline void decrease_sleep_time(struct f2fs_gc_kthread *gc_th,
							unsigned int *wait)
{
	unsigned int min_time = gc_th->min_sleep_time;

	if (*wait == gc_th->no_gc_sleep_time)
		*wait = gc_th->max_sleep_time;

	if ((long long)*wait - (long long)min_time < (long long)min_time)
		*wait = min_time;
	else
		*wait -= min_time;
}

static inline bool has_enough_invalid_blocks(struct f2fs_sb_info *sbi)
{
	block_t invalid_user_blocks = sbi->user_block_count -
					written_block_count(sbi);
	/*
	 * Background GC is triggered with the following conditions.
	 * 1. There are a number of invalid blocks.
	 * 2. There is not enough free space.
	 */
	if (invalid_user_blocks > limit_invalid_user_blocks(sbi) &&
			free_user_blocks(sbi) < limit_free_user_blocks(sbi))
		return true;
	return false;
}
