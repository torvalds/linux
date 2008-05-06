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
#include "ext4_jbd2.h"
#include "ext4.h"
#include "group.h"

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

static struct kmem_cache *ext4_pspace_cachep;
static struct kmem_cache *ext4_ac_cachep;

#ifdef EXT4_BB_MAX_BLOCKS
#undef EXT4_BB_MAX_BLOCKS
#endif
#define EXT4_BB_MAX_BLOCKS	30

struct ext4_free_metadata {
	ext4_group_t group;
	unsigned short num;
	ext4_grpblk_t  blocks[EXT4_BB_MAX_BLOCKS];
	struct list_head list;
};

struct ext4_group_info {
	unsigned long	bb_state;
	unsigned long	bb_tid;
	struct ext4_free_metadata *bb_md_cur;
	unsigned short	bb_first_free;
	unsigned short	bb_free;
	unsigned short	bb_fragments;
	struct		list_head bb_prealloc_list;
#ifdef DOUBLE_CHECK
	void		*bb_bitmap;
#endif
	unsigned short	bb_counters[];
};

#define EXT4_GROUP_INFO_NEED_INIT_BIT	0
#define EXT4_GROUP_INFO_LOCKED_BIT	1

#define EXT4_MB_GRP_NEED_INIT(grp)	\
	(test_bit(EXT4_GROUP_INFO_NEED_INIT_BIT, &((grp)->bb_state)))


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
	unsigned short		pa_linear;	/* consumed in one direction
						 * strictly, for grp prealloc */
	spinlock_t		*pa_obj_lock;
	struct inode		*pa_inode;	/* hack, for history only */
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
 */
struct ext4_locality_group {
	/* for allocator */
	struct mutex		lg_mutex;	/* to serialize allocates */
	struct list_head	lg_prealloc_list;/* list of preallocations */
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
};
#define EXT4_MB_BITMAP(e4b)	((e4b)->bd_bitmap)
#define EXT4_MB_BUDDY(e4b)	((e4b)->bd_buddy)

#ifndef EXT4_MB_HISTORY
static inline void ext4_mb_store_history(struct ext4_allocation_context *ac)
{
	return;
}
#else
static void ext4_mb_store_history(struct ext4_allocation_context *ac);
#endif

#define in_range(b, first, len)	((b) >= (first) && (b) <= (first) + (len) - 1)

static struct proc_dir_entry *proc_root_ext4;
struct buffer_head *read_block_bitmap(struct super_block *, ext4_group_t);

static void ext4_mb_generate_from_pa(struct super_block *sb, void *bitmap,
					ext4_group_t group);
static void ext4_mb_poll_new_transaction(struct super_block *, handle_t *);
static void ext4_mb_free_committed_blocks(struct super_block *);
static void ext4_mb_return_to_preallocation(struct inode *inode,
					struct ext4_buddy *e4b, sector_t block,
					int count);
static void ext4_mb_put_pa(struct ext4_allocation_context *,
			struct super_block *, struct ext4_prealloc_space *pa);
static int ext4_mb_init_per_dev_proc(struct super_block *sb);
static int ext4_mb_destroy_per_dev_proc(struct super_block *sb);


static inline void ext4_lock_group(struct super_block *sb, ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	bit_spin_lock(EXT4_GROUP_INFO_LOCKED_BIT, &(grinfo->bb_state));
}

static inline void ext4_unlock_group(struct super_block *sb,
					ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	bit_spin_unlock(EXT4_GROUP_INFO_LOCKED_BIT, &(grinfo->bb_state));
}

static inline int ext4_is_group_locked(struct super_block *sb,
					ext4_group_t group)
{
	struct ext4_group_info *grinfo = ext4_get_group_info(sb, group);

	return bit_spin_is_locked(EXT4_GROUP_INFO_LOCKED_BIT,
						&(grinfo->bb_state));
}

static ext4_fsblk_t ext4_grp_offs_to_block(struct super_block *sb,
					struct ext4_free_extent *fex)
{
	ext4_fsblk_t block;

	block = (ext4_fsblk_t) fex->fe_group * EXT4_BLOCKS_PER_GROUP(sb)
			+ fex->fe_start
			+ le32_to_cpu(EXT4_SB(sb)->s_es->s_first_data_block);
	return block;
}
#endif
