/*
 *  fs/ext4/mballoc.h
 *
 *  Written by: Alex Tomas <alex@clusterfs.com>
 *
 */
#ifndef _EXT4_MBALLOC_H
#define _EXT4_MBALLOC_H

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/marker.h>
#include <linux/mutex.h>
#include "ext4_jbd2.h"
#include "ext4.h"

/*
 * with AGGRESSIVE_CHECK allocator runs consistency checks over
 * structures. these checks slow things down a lot
 */
#define AGGRESSIVE_CHECK__

/*
 * with DOUBLE_CHECK defined mballoc creates persistent in-core
 * bitmaps, maintains and uses them to check for double allocations
 */
#define DOUBLE_CHECK__

/*
 */
#define MB_DEBUG__
#ifdef MB_DEBUG
#define mb_debug(fmt, a...)	printk(fmt, ##a)
#else
#define mb_debug(fmt, a...)
#endif

/*
 * with EXT4_MB_HISTORY mballoc stores last N allocations in memory
 * and you can monitor it in /proc/fs/ext4/<dev>/mb_history
 */
#define EXT4_MB_HISTORY
#define EXT4_MB_HISTORY_ALLOC		1	/* allocation */
#define EXT4_MB_HISTORY_PREALLOC	2	/* preallocated blocks used */
#define EXT4_MB_HISTORY_DISCARD		4	/* preallocation discarded */
#define EXT4_MB_HISTORY_FREE		8	/* free */

#define EXT4_MB_HISTORY_DEFAULT		(EXT4_MB_HISTORY_ALLOC | \
					 EXT4_MB_HISTORY_PREALLOC)

/*
 * How long mballoc can look for a best extent (in found extents)
 */
#define MB_DEFAULT_MAX_TO_SCAN		200

/*
 * How long mballoc must look for a best extent
 */
#define MB_DEFAULT_MIN_TO_SCAN		10

/*
 * How many groups mballoc will scan looking for the best chunk
 */
#define MB_DEFAULT_MAX_GROUPS_TO_SCAN	5

/*
 * with 'ext4_mb_stats' allocator will collect stats that will be
 * shown at umount. The collecting costs though!
 */
#define MB_DEFAULT_STATS		1

/*
 * files smaller than MB_DEFAULT_STREAM_THRESHOLD are served
 * by the stream allocator, which purpose is to pack requests
 * as close each to other as possible to produce smooth I/O traffic
 * We use locality group prealloc space for stream request.
 * We can tune the same via /proc/fs/ext4/<parition>/stream_req
 */
#define MB_DEFAULT_STREAM_THRESHOLD	16	/* 64K */

/*
 * for which requests use 2^N search using buddies
 */
#define MB_DEFAULT_ORDER2_REQS		2

/*
 * default group prealloc size 512 blocks
 */
#define MB_DEFAULT_GROUP_PREALLOC	512


struct ext4_free_data {
	/* this links the free block information from group_info */
	struct rb_node node;

	/* this links the free block information from ext4_sb_info */
	struct list_head list;

	/* group which free block extent belongs */
	ext4_group_t group;

	/* free block extent */
	ext4_grpblk_t start_blk;
	ext4_grpblk_t count;

	/* transaction which freed this extent */
	tid_t	t_tid;
};

struct ext4_prealloc_space {
	struct list_head	pa_inode_list;
	struct list_head	pa_group_list;
	union {
		struct list_head pa_tmp_list;
		struct rcu_head	pa_rcu;
	} u;
	spinlock_t		pa_lock;
	atomic_t		pa_count;
	unsigned		pa_deleted;
	ext4_fsblk_t		pa_pstart;	/* phys. block */
	ext4_lblk_t		pa_lstart;	/* log. block */
	unsigned short		pa_len;		/* len of preallocated chunk */
	unsigned short		pa_free;	/* how many blocks are free */
	unsigned short		pa_type;	/* pa type. inode or group */
	spinlock_t		*pa_obj_lock;
	struct inode		*pa_inode;	/* hack, for history only */
};

enum {
	MB_INODE_PA = 0,
	MB_GROUP_PA = 1
};

struct ext4_free_extent {
	ext4_lblk_t fe_logical;
	ext4_grpblk_t fe_start;
	ext4_group_t fe_group;
	int fe_len;
};

/*
 * Locality group:
 *   we try to group all related changes together
 *   so that writeback can flush/allocate them together as well
 *   Size of lg_prealloc_list hash is determined by MB_DEFAULT_GROUP_PREALLOC
 *   (512). We store prealloc space into the hash based on the pa_free blocks
 *   order value.ie, fls(pa_free)-1;
 */
#define PREALLOC_TB_SIZE 10
struct ext4_locality_group {
	/* for allocator */
	/* to serialize allocates */
	struct mutex		lg_mutex;
	/* list of preallocations */
	struct list_head	lg_prealloc_list[PREALLOC_TB_SIZE];
	spinlock_t		lg_prealloc_lock;
};

struct ext4_allocation_context {
	struct inode *ac_inode;
	struct super_block *ac_sb;

	/* original request */
	struct ext4_free_extent ac_o_ex;

	/* goal request (after normalization) */
	struct ext4_free_extent ac_g_ex;

	/* the best found extent */
	struct ext4_free_extent ac_b_ex;

	/* copy of the bext found extent taken before preallocation efforts */
	struct ext4_free_extent ac_f_ex;

	/* number of iterations done. we have to track to limit searching */
	unsigned long ac_ex_scanned;
	__u16 ac_groups_scanned;
	__u16 ac_found;
	__u16 ac_tail;
	__u16 ac_buddy;
	__u16 ac_flags;		/* allocation hints */
	__u8 ac_status;
	__u8 ac_criteria;
	__u8 ac_repeats;
	__u8 ac_2order;		/* if request is to allocate 2^N blocks and
				 * N > 0, the field stores N, otherwise 0 */
	__u8 ac_op;		/* operation, for history only */
	struct page *ac_bitmap_page;
	struct page *ac_buddy_page;
	/*
	 * pointer to the held semaphore upon successful
	 * block allocation
	 */
	struct rw_semaphore *alloc_semp;
	struct ext4_prealloc_space *ac_pa;
	struct ext4_locality_group *ac_lg;
};

#define AC_STATUS_CONTINUE	1
#define AC_STATUS_FOUND		2
#define AC_STATUS_BREAK		3

struct ext4_mb_history {
	struct ext4_free_extent orig;	/* orig allocation */
	struct ext4_free_extent goal;	/* goal allocation */
	struct ext4_free_extent result;	/* result allocation */
	unsigned pid;
	unsigned ino;
	__u16 found;	/* how many extents have been found */
	__u16 groups;	/* how many groups have been scanned */
	__u16 tail;	/* what tail broke some buddy */
	__u16 buddy;	/* buddy the tail ^^^ broke */
	__u16 flags;
	__u8 cr:3;	/* which phase the result extent was found at */
	__u8 op:4;
	__u8 merged:1;
};

struct ext4_buddy {
	struct page *bd_buddy_page;
	void *bd_buddy;
	struct page *bd_bitmap_page;
	void *bd_bitmap;
	struct ext4_group_info *bd_info;
	struct super_block *bd_sb;
	__u16 bd_blkbits;
	ext4_group_t bd_group;
	struct rw_semaphore *alloc_semp;
};
#define EXT4_MB_BITMAP(e4b)	((e4b)->bd_bitmap)
#define EXT4_MB_BUDDY(e4b)	((e4b)->bd_buddy)

#ifndef EXT4_MB_HISTORY
static inline void ext4_mb_store_history(struct ext4_allocation_context *ac)
{
	return;
}
#endif

#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

static inline ext4_fsblk_t ext4_grp_offs_to_block(struct super_block *sb,
					struct ext4_free_extent *fex)
{
	ext4_fsblk_t block;

	block = (ext4_fsblk_t) fex->fe_group * EXT4_BLOCKS_PER_GROUP(sb)
			+ fex->fe_start
			+ le32_to_cpu(EXT4_SB(sb)->s_es->s_first_data_block);
	return block;
}
#endif
