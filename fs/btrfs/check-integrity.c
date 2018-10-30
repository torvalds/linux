// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STRATO AG 2011.  All rights reserved.
 */

/*
 * This module can be used to catch cases when the btrfs kernel
 * code executes write requests to the disk that bring the file
 * system in an inconsistent state. In such a state, a power-loss
 * or kernel panic event would cause that the data on disk is
 * lost or at least damaged.
 *
 * Code is added that examines all block write requests during
 * runtime (including writes of the super block). Three rules
 * are verified and an error is printed on violation of the
 * rules:
 * 1. It is not allowed to write a disk block which is
 *    currently referenced by the super block (either directly
 *    or indirectly).
 * 2. When a super block is written, it is verified that all
 *    referenced (directly or indirectly) blocks fulfill the
 *    following requirements:
 *    2a. All referenced blocks have either been present when
 *        the file system was mounted, (i.e., they have been
 *        referenced by the super block) or they have been
 *        written since then and the write completion callback
 *        was called and no write error was indicated and a
 *        FLUSH request to the device where these blocks are
 *        located was received and completed.
 *    2b. All referenced blocks need to have a generation
 *        number which is equal to the parent's number.
 *
 * One issue that was found using this module was that the log
 * tree on disk became temporarily corrupted because disk blocks
 * that had been in use for the log tree had been freed and
 * reused too early, while being referenced by the written super
 * block.
 *
 * The search term in the kernel log that can be used to filter
 * on the existence of detected integrity issues is
 * "btrfs: attempt".
 *
 * The integrity check is enabled via mount options. These
 * mount options are only supported if the integrity check
 * tool is compiled by defining BTRFS_FS_CHECK_INTEGRITY.
 *
 * Example #1, apply integrity checks to all metadata:
 * mount /dev/sdb1 /mnt -o check_int
 *
 * Example #2, apply integrity checks to all metadata and
 * to data extents:
 * mount /dev/sdb1 /mnt -o check_int_data
 *
 * Example #3, apply integrity checks to all metadata and dump
 * the tree that the super block references to kernel messages
 * each time after a super block was written:
 * mount /dev/sdb1 /mnt -o check_int,check_int_print_mask=263
 *
 * If the integrity check tool is included and activated in
 * the mount options, plenty of kernel memory is used, and
 * plenty of additional CPU cycles are spent. Enabling this
 * functionality is not intended for normal use. In most
 * cases, unless you are a btrfs developer who needs to verify
 * the integrity of (super)-block write requests, do not
 * enable the config option BTRFS_FS_CHECK_INTEGRITY to
 * include and compile the integrity check tool.
 *
 * Expect millions of lines of information in the kernel log with an
 * enabled check_int_print_mask. Therefore set LOG_BUF_SHIFT in the
 * kernel config to at least 26 (which is 64MB). Usually the value is
 * limited to 21 (which is 2MB) in init/Kconfig. The file needs to be
 * changed like this before LOG_BUF_SHIFT can be set to a high value:
 * config LOG_BUF_SHIFT
 *       int "Kernel log buffer size (16 => 64KB, 17 => 128KB)"
 *       range 12 30
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/mutex.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/crc32c.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "extent_io.h"
#include "volumes.h"
#include "print-tree.h"
#include "locking.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "compression.h"

#define BTRFSIC_BLOCK_HASHTABLE_SIZE 0x10000
#define BTRFSIC_BLOCK_LINK_HASHTABLE_SIZE 0x10000
#define BTRFSIC_DEV2STATE_HASHTABLE_SIZE 0x100
#define BTRFSIC_BLOCK_MAGIC_NUMBER 0x14491051
#define BTRFSIC_BLOCK_LINK_MAGIC_NUMBER 0x11070807
#define BTRFSIC_DEV2STATE_MAGIC_NUMBER 0x20111530
#define BTRFSIC_BLOCK_STACK_FRAME_MAGIC_NUMBER 20111300
#define BTRFSIC_TREE_DUMP_MAX_INDENT_LEVEL (200 - 6)	/* in characters,
							 * excluding " [...]" */
#define BTRFSIC_GENERATION_UNKNOWN ((u64)-1)

/*
 * The definition of the bitmask fields for the print_mask.
 * They are specified with the mount option check_integrity_print_mask.
 */
#define BTRFSIC_PRINT_MASK_SUPERBLOCK_WRITE			0x00000001
#define BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION		0x00000002
#define BTRFSIC_PRINT_MASK_TREE_AFTER_SB_WRITE			0x00000004
#define BTRFSIC_PRINT_MASK_TREE_BEFORE_SB_WRITE			0x00000008
#define BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH			0x00000010
#define BTRFSIC_PRINT_MASK_END_IO_BIO_BH			0x00000020
#define BTRFSIC_PRINT_MASK_VERBOSE				0x00000040
#define BTRFSIC_PRINT_MASK_VERY_VERBOSE				0x00000080
#define BTRFSIC_PRINT_MASK_INITIAL_TREE				0x00000100
#define BTRFSIC_PRINT_MASK_INITIAL_ALL_TREES			0x00000200
#define BTRFSIC_PRINT_MASK_INITIAL_DATABASE			0x00000400
#define BTRFSIC_PRINT_MASK_NUM_COPIES				0x00000800
#define BTRFSIC_PRINT_MASK_TREE_WITH_ALL_MIRRORS		0x00001000
#define BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH_VERBOSE		0x00002000

struct btrfsic_dev_state;
struct btrfsic_state;

struct btrfsic_block {
	u32 magic_num;		/* only used for debug purposes */
	unsigned int is_metadata:1;	/* if it is meta-data, not data-data */
	unsigned int is_superblock:1;	/* if it is one of the superblocks */
	unsigned int is_iodone:1;	/* if is done by lower subsystem */
	unsigned int iodone_w_error:1;	/* error was indicated to endio */
	unsigned int never_written:1;	/* block was added because it was
					 * referenced, not because it was
					 * written */
	unsigned int mirror_num;	/* large enough to hold
					 * BTRFS_SUPER_MIRROR_MAX */
	struct btrfsic_dev_state *dev_state;
	u64 dev_bytenr;		/* key, physical byte num on disk */
	u64 logical_bytenr;	/* logical byte num on disk */
	u64 generation;
	struct btrfs_disk_key disk_key;	/* extra info to print in case of
					 * issues, will not always be correct */
	struct list_head collision_resolving_node;	/* list node */
	struct list_head all_blocks_node;	/* list node */

	/* the following two lists contain block_link items */
	struct list_head ref_to_list;	/* list */
	struct list_head ref_from_list;	/* list */
	struct btrfsic_block *next_in_same_bio;
	void *orig_bio_bh_private;
	union {
		bio_end_io_t *bio;
		bh_end_io_t *bh;
	} orig_bio_bh_end_io;
	int submit_bio_bh_rw;
	u64 flush_gen; /* only valid if !never_written */
};

/*
 * Elements of this type are allocated dynamically and required because
 * each block object can refer to and can be ref from multiple blocks.
 * The key to lookup them in the hashtable is the dev_bytenr of
 * the block ref to plus the one from the block referred from.
 * The fact that they are searchable via a hashtable and that a
 * ref_cnt is maintained is not required for the btrfs integrity
 * check algorithm itself, it is only used to make the output more
 * beautiful in case that an error is detected (an error is defined
 * as a write operation to a block while that block is still referenced).
 */
struct btrfsic_block_link {
	u32 magic_num;		/* only used for debug purposes */
	u32 ref_cnt;
	struct list_head node_ref_to;	/* list node */
	struct list_head node_ref_from;	/* list node */
	struct list_head collision_resolving_node;	/* list node */
	struct btrfsic_block *block_ref_to;
	struct btrfsic_block *block_ref_from;
	u64 parent_generation;
};

struct btrfsic_dev_state {
	u32 magic_num;		/* only used for debug purposes */
	struct block_device *bdev;
	struct btrfsic_state *state;
	struct list_head collision_resolving_node;	/* list node */
	struct btrfsic_block dummy_block_for_bio_bh_flush;
	u64 last_flush_gen;
	char name[BDEVNAME_SIZE];
};

struct btrfsic_block_hashtable {
	struct list_head table[BTRFSIC_BLOCK_HASHTABLE_SIZE];
};

struct btrfsic_block_link_hashtable {
	struct list_head table[BTRFSIC_BLOCK_LINK_HASHTABLE_SIZE];
};

struct btrfsic_dev_state_hashtable {
	struct list_head table[BTRFSIC_DEV2STATE_HASHTABLE_SIZE];
};

struct btrfsic_block_data_ctx {
	u64 start;		/* virtual bytenr */
	u64 dev_bytenr;		/* physical bytenr on device */
	u32 len;
	struct btrfsic_dev_state *dev;
	char **datav;
	struct page **pagev;
	void *mem_to_free;
};

/* This structure is used to implement recursion without occupying
 * any stack space, refer to btrfsic_process_metablock() */
struct btrfsic_stack_frame {
	u32 magic;
	u32 nr;
	int error;
	int i;
	int limit_nesting;
	int num_copies;
	int mirror_num;
	struct btrfsic_block *block;
	struct btrfsic_block_data_ctx *block_ctx;
	struct btrfsic_block *next_block;
	struct btrfsic_block_data_ctx next_block_ctx;
	struct btrfs_header *hdr;
	struct btrfsic_stack_frame *prev;
};

/* Some state per mounted filesystem */
struct btrfsic_state {
	u32 print_mask;
	int include_extent_data;
	int csum_size;
	struct list_head all_blocks_list;
	struct btrfsic_block_hashtable block_hashtable;
	struct btrfsic_block_link_hashtable block_link_hashtable;
	struct btrfs_fs_info *fs_info;
	u64 max_superblock_generation;
	struct btrfsic_block *latest_superblock;
	u32 metablock_size;
	u32 datablock_size;
};

static void btrfsic_block_init(struct btrfsic_block *b);
static struct btrfsic_block *btrfsic_block_alloc(void);
static void btrfsic_block_free(struct btrfsic_block *b);
static void btrfsic_block_link_init(struct btrfsic_block_link *n);
static struct btrfsic_block_link *btrfsic_block_link_alloc(void);
static void btrfsic_block_link_free(struct btrfsic_block_link *n);
static void btrfsic_dev_state_init(struct btrfsic_dev_state *ds);
static struct btrfsic_dev_state *btrfsic_dev_state_alloc(void);
static void btrfsic_dev_state_free(struct btrfsic_dev_state *ds);
static void btrfsic_block_hashtable_init(struct btrfsic_block_hashtable *h);
static void btrfsic_block_hashtable_add(struct btrfsic_block *b,
					struct btrfsic_block_hashtable *h);
static void btrfsic_block_hashtable_remove(struct btrfsic_block *b);
static struct btrfsic_block *btrfsic_block_hashtable_lookup(
		struct block_device *bdev,
		u64 dev_bytenr,
		struct btrfsic_block_hashtable *h);
static void btrfsic_block_link_hashtable_init(
		struct btrfsic_block_link_hashtable *h);
static void btrfsic_block_link_hashtable_add(
		struct btrfsic_block_link *l,
		struct btrfsic_block_link_hashtable *h);
static void btrfsic_block_link_hashtable_remove(struct btrfsic_block_link *l);
static struct btrfsic_block_link *btrfsic_block_link_hashtable_lookup(
		struct block_device *bdev_ref_to,
		u64 dev_bytenr_ref_to,
		struct block_device *bdev_ref_from,
		u64 dev_bytenr_ref_from,
		struct btrfsic_block_link_hashtable *h);
static void btrfsic_dev_state_hashtable_init(
		struct btrfsic_dev_state_hashtable *h);
static void btrfsic_dev_state_hashtable_add(
		struct btrfsic_dev_state *ds,
		struct btrfsic_dev_state_hashtable *h);
static void btrfsic_dev_state_hashtable_remove(struct btrfsic_dev_state *ds);
static struct btrfsic_dev_state *btrfsic_dev_state_hashtable_lookup(dev_t dev,
		struct btrfsic_dev_state_hashtable *h);
static struct btrfsic_stack_frame *btrfsic_stack_frame_alloc(void);
static void btrfsic_stack_frame_free(struct btrfsic_stack_frame *sf);
static int btrfsic_process_superblock(struct btrfsic_state *state,
				      struct btrfs_fs_devices *fs_devices);
static int btrfsic_process_metablock(struct btrfsic_state *state,
				     struct btrfsic_block *block,
				     struct btrfsic_block_data_ctx *block_ctx,
				     int limit_nesting, int force_iodone_flag);
static void btrfsic_read_from_block_data(
	struct btrfsic_block_data_ctx *block_ctx,
	void *dst, u32 offset, size_t len);
static int btrfsic_create_link_to_next_block(
		struct btrfsic_state *state,
		struct btrfsic_block *block,
		struct btrfsic_block_data_ctx
		*block_ctx, u64 next_bytenr,
		int limit_nesting,
		struct btrfsic_block_data_ctx *next_block_ctx,
		struct btrfsic_block **next_blockp,
		int force_iodone_flag,
		int *num_copiesp, int *mirror_nump,
		struct btrfs_disk_key *disk_key,
		u64 parent_generation);
static int btrfsic_handle_extent_data(struct btrfsic_state *state,
				      struct btrfsic_block *block,
				      struct btrfsic_block_data_ctx *block_ctx,
				      u32 item_offset, int force_iodone_flag);
static int btrfsic_map_block(struct btrfsic_state *state, u64 bytenr, u32 len,
			     struct btrfsic_block_data_ctx *block_ctx_out,
			     int mirror_num);
static void btrfsic_release_block_ctx(struct btrfsic_block_data_ctx *block_ctx);
static int btrfsic_read_block(struct btrfsic_state *state,
			      struct btrfsic_block_data_ctx *block_ctx);
static void btrfsic_dump_database(struct btrfsic_state *state);
static int btrfsic_test_for_metadata(struct btrfsic_state *state,
				     char **datav, unsigned int num_pages);
static void btrfsic_process_written_block(struct btrfsic_dev_state *dev_state,
					  u64 dev_bytenr, char **mapped_datav,
					  unsigned int num_pages,
					  struct bio *bio, int *bio_is_patched,
					  struct buffer_head *bh,
					  int submit_bio_bh_rw);
static int btrfsic_process_written_superblock(
		struct btrfsic_state *state,
		struct btrfsic_block *const block,
		struct btrfs_super_block *const super_hdr);
static void btrfsic_bio_end_io(struct bio *bp);
static void btrfsic_bh_end_io(struct buffer_head *bh, int uptodate);
static int btrfsic_is_block_ref_by_superblock(const struct btrfsic_state *state,
					      const struct btrfsic_block *block,
					      int recursion_level);
static int btrfsic_check_all_ref_blocks(struct btrfsic_state *state,
					struct btrfsic_block *const block,
					int recursion_level);
static void btrfsic_print_add_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l);
static void btrfsic_print_rem_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l);
static char btrfsic_get_block_type(const struct btrfsic_state *state,
				   const struct btrfsic_block *block);
static void btrfsic_dump_tree(const struct btrfsic_state *state);
static void btrfsic_dump_tree_sub(const struct btrfsic_state *state,
				  const struct btrfsic_block *block,
				  int indent_level);
static struct btrfsic_block_link *btrfsic_block_link_lookup_or_add(
		struct btrfsic_state *state,
		struct btrfsic_block_data_ctx *next_block_ctx,
		struct btrfsic_block *next_block,
		struct btrfsic_block *from_block,
		u64 parent_generation);
static struct btrfsic_block *btrfsic_block_lookup_or_add(
		struct btrfsic_state *state,
		struct btrfsic_block_data_ctx *block_ctx,
		const char *additional_string,
		int is_metadata,
		int is_iodone,
		int never_written,
		int mirror_num,
		int *was_created);
static int btrfsic_process_superblock_dev_mirror(
		struct btrfsic_state *state,
		struct btrfsic_dev_state *dev_state,
		struct btrfs_device *device,
		int superblock_mirror_num,
		struct btrfsic_dev_state **selected_dev_state,
		struct btrfs_super_block *selected_super);
static struct btrfsic_dev_state *btrfsic_dev_state_lookup(dev_t dev);
static void btrfsic_cmp_log_and_dev_bytenr(struct btrfsic_state *state,
					   u64 bytenr,
					   struct btrfsic_dev_state *dev_state,
					   u64 dev_bytenr);

static struct mutex btrfsic_mutex;
static int btrfsic_is_initialized;
static struct btrfsic_dev_state_hashtable btrfsic_dev_state_hashtable;


static void btrfsic_block_init(struct btrfsic_block *b)
{
	b->magic_num = BTRFSIC_BLOCK_MAGIC_NUMBER;
	b->dev_state = NULL;
	b->dev_bytenr = 0;
	b->logical_bytenr = 0;
	b->generation = BTRFSIC_GENERATION_UNKNOWN;
	b->disk_key.objectid = 0;
	b->disk_key.type = 0;
	b->disk_key.offset = 0;
	b->is_metadata = 0;
	b->is_superblock = 0;
	b->is_iodone = 0;
	b->iodone_w_error = 0;
	b->never_written = 0;
	b->mirror_num = 0;
	b->next_in_same_bio = NULL;
	b->orig_bio_bh_private = NULL;
	b->orig_bio_bh_end_io.bio = NULL;
	INIT_LIST_HEAD(&b->collision_resolving_node);
	INIT_LIST_HEAD(&b->all_blocks_node);
	INIT_LIST_HEAD(&b->ref_to_list);
	INIT_LIST_HEAD(&b->ref_from_list);
	b->submit_bio_bh_rw = 0;
	b->flush_gen = 0;
}

static struct btrfsic_block *btrfsic_block_alloc(void)
{
	struct btrfsic_block *b;

	b = kzalloc(sizeof(*b), GFP_NOFS);
	if (NULL != b)
		btrfsic_block_init(b);

	return b;
}

static void btrfsic_block_free(struct btrfsic_block *b)
{
	BUG_ON(!(NULL == b || BTRFSIC_BLOCK_MAGIC_NUMBER == b->magic_num));
	kfree(b);
}

static void btrfsic_block_link_init(struct btrfsic_block_link *l)
{
	l->magic_num = BTRFSIC_BLOCK_LINK_MAGIC_NUMBER;
	l->ref_cnt = 1;
	INIT_LIST_HEAD(&l->node_ref_to);
	INIT_LIST_HEAD(&l->node_ref_from);
	INIT_LIST_HEAD(&l->collision_resolving_node);
	l->block_ref_to = NULL;
	l->block_ref_from = NULL;
}

static struct btrfsic_block_link *btrfsic_block_link_alloc(void)
{
	struct btrfsic_block_link *l;

	l = kzalloc(sizeof(*l), GFP_NOFS);
	if (NULL != l)
		btrfsic_block_link_init(l);

	return l;
}

static void btrfsic_block_link_free(struct btrfsic_block_link *l)
{
	BUG_ON(!(NULL == l || BTRFSIC_BLOCK_LINK_MAGIC_NUMBER == l->magic_num));
	kfree(l);
}

static void btrfsic_dev_state_init(struct btrfsic_dev_state *ds)
{
	ds->magic_num = BTRFSIC_DEV2STATE_MAGIC_NUMBER;
	ds->bdev = NULL;
	ds->state = NULL;
	ds->name[0] = '\0';
	INIT_LIST_HEAD(&ds->collision_resolving_node);
	ds->last_flush_gen = 0;
	btrfsic_block_init(&ds->dummy_block_for_bio_bh_flush);
	ds->dummy_block_for_bio_bh_flush.is_iodone = 1;
	ds->dummy_block_for_bio_bh_flush.dev_state = ds;
}

static struct btrfsic_dev_state *btrfsic_dev_state_alloc(void)
{
	struct btrfsic_dev_state *ds;

	ds = kzalloc(sizeof(*ds), GFP_NOFS);
	if (NULL != ds)
		btrfsic_dev_state_init(ds);

	return ds;
}

static void btrfsic_dev_state_free(struct btrfsic_dev_state *ds)
{
	BUG_ON(!(NULL == ds ||
		 BTRFSIC_DEV2STATE_MAGIC_NUMBER == ds->magic_num));
	kfree(ds);
}

static void btrfsic_block_hashtable_init(struct btrfsic_block_hashtable *h)
{
	int i;

	for (i = 0; i < BTRFSIC_BLOCK_HASHTABLE_SIZE; i++)
		INIT_LIST_HEAD(h->table + i);
}

static void btrfsic_block_hashtable_add(struct btrfsic_block *b,
					struct btrfsic_block_hashtable *h)
{
	const unsigned int hashval =
	    (((unsigned int)(b->dev_bytenr >> 16)) ^
	     ((unsigned int)((uintptr_t)b->dev_state->bdev))) &
	     (BTRFSIC_BLOCK_HASHTABLE_SIZE - 1);

	list_add(&b->collision_resolving_node, h->table + hashval);
}

static void btrfsic_block_hashtable_remove(struct btrfsic_block *b)
{
	list_del(&b->collision_resolving_node);
}

static struct btrfsic_block *btrfsic_block_hashtable_lookup(
		struct block_device *bdev,
		u64 dev_bytenr,
		struct btrfsic_block_hashtable *h)
{
	const unsigned int hashval =
	    (((unsigned int)(dev_bytenr >> 16)) ^
	     ((unsigned int)((uintptr_t)bdev))) &
	     (BTRFSIC_BLOCK_HASHTABLE_SIZE - 1);
	struct btrfsic_block *b;

	list_for_each_entry(b, h->table + hashval, collision_resolving_node) {
		if (b->dev_state->bdev == bdev && b->dev_bytenr == dev_bytenr)
			return b;
	}

	return NULL;
}

static void btrfsic_block_link_hashtable_init(
		struct btrfsic_block_link_hashtable *h)
{
	int i;

	for (i = 0; i < BTRFSIC_BLOCK_LINK_HASHTABLE_SIZE; i++)
		INIT_LIST_HEAD(h->table + i);
}

static void btrfsic_block_link_hashtable_add(
		struct btrfsic_block_link *l,
		struct btrfsic_block_link_hashtable *h)
{
	const unsigned int hashval =
	    (((unsigned int)(l->block_ref_to->dev_bytenr >> 16)) ^
	     ((unsigned int)(l->block_ref_from->dev_bytenr >> 16)) ^
	     ((unsigned int)((uintptr_t)l->block_ref_to->dev_state->bdev)) ^
	     ((unsigned int)((uintptr_t)l->block_ref_from->dev_state->bdev)))
	     & (BTRFSIC_BLOCK_LINK_HASHTABLE_SIZE - 1);

	BUG_ON(NULL == l->block_ref_to);
	BUG_ON(NULL == l->block_ref_from);
	list_add(&l->collision_resolving_node, h->table + hashval);
}

static void btrfsic_block_link_hashtable_remove(struct btrfsic_block_link *l)
{
	list_del(&l->collision_resolving_node);
}

static struct btrfsic_block_link *btrfsic_block_link_hashtable_lookup(
		struct block_device *bdev_ref_to,
		u64 dev_bytenr_ref_to,
		struct block_device *bdev_ref_from,
		u64 dev_bytenr_ref_from,
		struct btrfsic_block_link_hashtable *h)
{
	const unsigned int hashval =
	    (((unsigned int)(dev_bytenr_ref_to >> 16)) ^
	     ((unsigned int)(dev_bytenr_ref_from >> 16)) ^
	     ((unsigned int)((uintptr_t)bdev_ref_to)) ^
	     ((unsigned int)((uintptr_t)bdev_ref_from))) &
	     (BTRFSIC_BLOCK_LINK_HASHTABLE_SIZE - 1);
	struct btrfsic_block_link *l;

	list_for_each_entry(l, h->table + hashval, collision_resolving_node) {
		BUG_ON(NULL == l->block_ref_to);
		BUG_ON(NULL == l->block_ref_from);
		if (l->block_ref_to->dev_state->bdev == bdev_ref_to &&
		    l->block_ref_to->dev_bytenr == dev_bytenr_ref_to &&
		    l->block_ref_from->dev_state->bdev == bdev_ref_from &&
		    l->block_ref_from->dev_bytenr == dev_bytenr_ref_from)
			return l;
	}

	return NULL;
}

static void btrfsic_dev_state_hashtable_init(
		struct btrfsic_dev_state_hashtable *h)
{
	int i;

	for (i = 0; i < BTRFSIC_DEV2STATE_HASHTABLE_SIZE; i++)
		INIT_LIST_HEAD(h->table + i);
}

static void btrfsic_dev_state_hashtable_add(
		struct btrfsic_dev_state *ds,
		struct btrfsic_dev_state_hashtable *h)
{
	const unsigned int hashval =
	    (((unsigned int)((uintptr_t)ds->bdev->bd_dev)) &
	     (BTRFSIC_DEV2STATE_HASHTABLE_SIZE - 1));

	list_add(&ds->collision_resolving_node, h->table + hashval);
}

static void btrfsic_dev_state_hashtable_remove(struct btrfsic_dev_state *ds)
{
	list_del(&ds->collision_resolving_node);
}

static struct btrfsic_dev_state *btrfsic_dev_state_hashtable_lookup(dev_t dev,
		struct btrfsic_dev_state_hashtable *h)
{
	const unsigned int hashval =
		dev & (BTRFSIC_DEV2STATE_HASHTABLE_SIZE - 1);
	struct btrfsic_dev_state *ds;

	list_for_each_entry(ds, h->table + hashval, collision_resolving_node) {
		if (ds->bdev->bd_dev == dev)
			return ds;
	}

	return NULL;
}

static int btrfsic_process_superblock(struct btrfsic_state *state,
				      struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfs_super_block *selected_super;
	struct list_head *dev_head = &fs_devices->devices;
	struct btrfs_device *device;
	struct btrfsic_dev_state *selected_dev_state = NULL;
	int ret = 0;
	int pass;

	BUG_ON(NULL == state);
	selected_super = kzalloc(sizeof(*selected_super), GFP_NOFS);
	if (NULL == selected_super) {
		pr_info("btrfsic: error, kmalloc failed!\n");
		return -ENOMEM;
	}

	list_for_each_entry(device, dev_head, dev_list) {
		int i;
		struct btrfsic_dev_state *dev_state;

		if (!device->bdev || !device->name)
			continue;

		dev_state = btrfsic_dev_state_lookup(device->bdev->bd_dev);
		BUG_ON(NULL == dev_state);
		for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
			ret = btrfsic_process_superblock_dev_mirror(
					state, dev_state, device, i,
					&selected_dev_state, selected_super);
			if (0 != ret && 0 == i) {
				kfree(selected_super);
				return ret;
			}
		}
	}

	if (NULL == state->latest_superblock) {
		pr_info("btrfsic: no superblock found!\n");
		kfree(selected_super);
		return -1;
	}

	state->csum_size = btrfs_super_csum_size(selected_super);

	for (pass = 0; pass < 3; pass++) {
		int num_copies;
		int mirror_num;
		u64 next_bytenr;

		switch (pass) {
		case 0:
			next_bytenr = btrfs_super_root(selected_super);
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("root@%llu\n", next_bytenr);
			break;
		case 1:
			next_bytenr = btrfs_super_chunk_root(selected_super);
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("chunk@%llu\n", next_bytenr);
			break;
		case 2:
			next_bytenr = btrfs_super_log_root(selected_super);
			if (0 == next_bytenr)
				continue;
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("log@%llu\n", next_bytenr);
			break;
		}

		num_copies = btrfs_num_copies(fs_info, next_bytenr,
					      state->metablock_size);
		if (state->print_mask & BTRFSIC_PRINT_MASK_NUM_COPIES)
			pr_info("num_copies(log_bytenr=%llu) = %d\n",
			       next_bytenr, num_copies);

		for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
			struct btrfsic_block *next_block;
			struct btrfsic_block_data_ctx tmp_next_block_ctx;
			struct btrfsic_block_link *l;

			ret = btrfsic_map_block(state, next_bytenr,
						state->metablock_size,
						&tmp_next_block_ctx,
						mirror_num);
			if (ret) {
				pr_info("btrfsic: btrfsic_map_block(root @%llu, mirror %d) failed!\n",
				       next_bytenr, mirror_num);
				kfree(selected_super);
				return -1;
			}

			next_block = btrfsic_block_hashtable_lookup(
					tmp_next_block_ctx.dev->bdev,
					tmp_next_block_ctx.dev_bytenr,
					&state->block_hashtable);
			BUG_ON(NULL == next_block);

			l = btrfsic_block_link_hashtable_lookup(
					tmp_next_block_ctx.dev->bdev,
					tmp_next_block_ctx.dev_bytenr,
					state->latest_superblock->dev_state->
					bdev,
					state->latest_superblock->dev_bytenr,
					&state->block_link_hashtable);
			BUG_ON(NULL == l);

			ret = btrfsic_read_block(state, &tmp_next_block_ctx);
			if (ret < (int)PAGE_SIZE) {
				pr_info("btrfsic: read @logical %llu failed!\n",
				       tmp_next_block_ctx.start);
				btrfsic_release_block_ctx(&tmp_next_block_ctx);
				kfree(selected_super);
				return -1;
			}

			ret = btrfsic_process_metablock(state,
							next_block,
							&tmp_next_block_ctx,
							BTRFS_MAX_LEVEL + 3, 1);
			btrfsic_release_block_ctx(&tmp_next_block_ctx);
		}
	}

	kfree(selected_super);
	return ret;
}

static int btrfsic_process_superblock_dev_mirror(
		struct btrfsic_state *state,
		struct btrfsic_dev_state *dev_state,
		struct btrfs_device *device,
		int superblock_mirror_num,
		struct btrfsic_dev_state **selected_dev_state,
		struct btrfs_super_block *selected_super)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfs_super_block *super_tmp;
	u64 dev_bytenr;
	struct buffer_head *bh;
	struct btrfsic_block *superblock_tmp;
	int pass;
	struct block_device *const superblock_bdev = device->bdev;

	/* super block bytenr is always the unmapped device bytenr */
	dev_bytenr = btrfs_sb_offset(superblock_mirror_num);
	if (dev_bytenr + BTRFS_SUPER_INFO_SIZE > device->commit_total_bytes)
		return -1;
	bh = __bread(superblock_bdev, dev_bytenr / BTRFS_BDEV_BLOCKSIZE,
		     BTRFS_SUPER_INFO_SIZE);
	if (NULL == bh)
		return -1;
	super_tmp = (struct btrfs_super_block *)
	    (bh->b_data + (dev_bytenr & (BTRFS_BDEV_BLOCKSIZE - 1)));

	if (btrfs_super_bytenr(super_tmp) != dev_bytenr ||
	    btrfs_super_magic(super_tmp) != BTRFS_MAGIC ||
	    memcmp(device->uuid, super_tmp->dev_item.uuid, BTRFS_UUID_SIZE) ||
	    btrfs_super_nodesize(super_tmp) != state->metablock_size ||
	    btrfs_super_sectorsize(super_tmp) != state->datablock_size) {
		brelse(bh);
		return 0;
	}

	superblock_tmp =
	    btrfsic_block_hashtable_lookup(superblock_bdev,
					   dev_bytenr,
					   &state->block_hashtable);
	if (NULL == superblock_tmp) {
		superblock_tmp = btrfsic_block_alloc();
		if (NULL == superblock_tmp) {
			pr_info("btrfsic: error, kmalloc failed!\n");
			brelse(bh);
			return -1;
		}
		/* for superblock, only the dev_bytenr makes sense */
		superblock_tmp->dev_bytenr = dev_bytenr;
		superblock_tmp->dev_state = dev_state;
		superblock_tmp->logical_bytenr = dev_bytenr;
		superblock_tmp->generation = btrfs_super_generation(super_tmp);
		superblock_tmp->is_metadata = 1;
		superblock_tmp->is_superblock = 1;
		superblock_tmp->is_iodone = 1;
		superblock_tmp->never_written = 0;
		superblock_tmp->mirror_num = 1 + superblock_mirror_num;
		if (state->print_mask & BTRFSIC_PRINT_MASK_SUPERBLOCK_WRITE)
			btrfs_info_in_rcu(fs_info,
				"new initial S-block (bdev %p, %s) @%llu (%s/%llu/%d)",
				     superblock_bdev,
				     rcu_str_deref(device->name), dev_bytenr,
				     dev_state->name, dev_bytenr,
				     superblock_mirror_num);
		list_add(&superblock_tmp->all_blocks_node,
			 &state->all_blocks_list);
		btrfsic_block_hashtable_add(superblock_tmp,
					    &state->block_hashtable);
	}

	/* select the one with the highest generation field */
	if (btrfs_super_generation(super_tmp) >
	    state->max_superblock_generation ||
	    0 == state->max_superblock_generation) {
		memcpy(selected_super, super_tmp, sizeof(*selected_super));
		*selected_dev_state = dev_state;
		state->max_superblock_generation =
		    btrfs_super_generation(super_tmp);
		state->latest_superblock = superblock_tmp;
	}

	for (pass = 0; pass < 3; pass++) {
		u64 next_bytenr;
		int num_copies;
		int mirror_num;
		const char *additional_string = NULL;
		struct btrfs_disk_key tmp_disk_key;

		tmp_disk_key.type = BTRFS_ROOT_ITEM_KEY;
		tmp_disk_key.offset = 0;
		switch (pass) {
		case 0:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_ROOT_TREE_OBJECTID);
			additional_string = "initial root ";
			next_bytenr = btrfs_super_root(super_tmp);
			break;
		case 1:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_CHUNK_TREE_OBJECTID);
			additional_string = "initial chunk ";
			next_bytenr = btrfs_super_chunk_root(super_tmp);
			break;
		case 2:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_TREE_LOG_OBJECTID);
			additional_string = "initial log ";
			next_bytenr = btrfs_super_log_root(super_tmp);
			if (0 == next_bytenr)
				continue;
			break;
		}

		num_copies = btrfs_num_copies(fs_info, next_bytenr,
					      state->metablock_size);
		if (state->print_mask & BTRFSIC_PRINT_MASK_NUM_COPIES)
			pr_info("num_copies(log_bytenr=%llu) = %d\n",
			       next_bytenr, num_copies);
		for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
			struct btrfsic_block *next_block;
			struct btrfsic_block_data_ctx tmp_next_block_ctx;
			struct btrfsic_block_link *l;

			if (btrfsic_map_block(state, next_bytenr,
					      state->metablock_size,
					      &tmp_next_block_ctx,
					      mirror_num)) {
				pr_info("btrfsic: btrfsic_map_block(bytenr @%llu, mirror %d) failed!\n",
				       next_bytenr, mirror_num);
				brelse(bh);
				return -1;
			}

			next_block = btrfsic_block_lookup_or_add(
					state, &tmp_next_block_ctx,
					additional_string, 1, 1, 0,
					mirror_num, NULL);
			if (NULL == next_block) {
				btrfsic_release_block_ctx(&tmp_next_block_ctx);
				brelse(bh);
				return -1;
			}

			next_block->disk_key = tmp_disk_key;
			next_block->generation = BTRFSIC_GENERATION_UNKNOWN;
			l = btrfsic_block_link_lookup_or_add(
					state, &tmp_next_block_ctx,
					next_block, superblock_tmp,
					BTRFSIC_GENERATION_UNKNOWN);
			btrfsic_release_block_ctx(&tmp_next_block_ctx);
			if (NULL == l) {
				brelse(bh);
				return -1;
			}
		}
	}
	if (state->print_mask & BTRFSIC_PRINT_MASK_INITIAL_ALL_TREES)
		btrfsic_dump_tree_sub(state, superblock_tmp, 0);

	brelse(bh);
	return 0;
}

static struct btrfsic_stack_frame *btrfsic_stack_frame_alloc(void)
{
	struct btrfsic_stack_frame *sf;

	sf = kzalloc(sizeof(*sf), GFP_NOFS);
	if (NULL == sf)
		pr_info("btrfsic: alloc memory failed!\n");
	else
		sf->magic = BTRFSIC_BLOCK_STACK_FRAME_MAGIC_NUMBER;
	return sf;
}

static void btrfsic_stack_frame_free(struct btrfsic_stack_frame *sf)
{
	BUG_ON(!(NULL == sf ||
		 BTRFSIC_BLOCK_STACK_FRAME_MAGIC_NUMBER == sf->magic));
	kfree(sf);
}

static int btrfsic_process_metablock(
		struct btrfsic_state *state,
		struct btrfsic_block *const first_block,
		struct btrfsic_block_data_ctx *const first_block_ctx,
		int first_limit_nesting, int force_iodone_flag)
{
	struct btrfsic_stack_frame initial_stack_frame = { 0 };
	struct btrfsic_stack_frame *sf;
	struct btrfsic_stack_frame *next_stack;
	struct btrfs_header *const first_hdr =
		(struct btrfs_header *)first_block_ctx->datav[0];

	BUG_ON(!first_hdr);
	sf = &initial_stack_frame;
	sf->error = 0;
	sf->i = -1;
	sf->limit_nesting = first_limit_nesting;
	sf->block = first_block;
	sf->block_ctx = first_block_ctx;
	sf->next_block = NULL;
	sf->hdr = first_hdr;
	sf->prev = NULL;

continue_with_new_stack_frame:
	sf->block->generation = le64_to_cpu(sf->hdr->generation);
	if (0 == sf->hdr->level) {
		struct btrfs_leaf *const leafhdr =
		    (struct btrfs_leaf *)sf->hdr;

		if (-1 == sf->i) {
			sf->nr = btrfs_stack_header_nritems(&leafhdr->header);

			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("leaf %llu items %d generation %llu owner %llu\n",
				       sf->block_ctx->start, sf->nr,
				       btrfs_stack_header_generation(
					       &leafhdr->header),
				       btrfs_stack_header_owner(
					       &leafhdr->header));
		}

continue_with_current_leaf_stack_frame:
		if (0 == sf->num_copies || sf->mirror_num > sf->num_copies) {
			sf->i++;
			sf->num_copies = 0;
		}

		if (sf->i < sf->nr) {
			struct btrfs_item disk_item;
			u32 disk_item_offset =
				(uintptr_t)(leafhdr->items + sf->i) -
				(uintptr_t)leafhdr;
			struct btrfs_disk_key *disk_key;
			u8 type;
			u32 item_offset;
			u32 item_size;

			if (disk_item_offset + sizeof(struct btrfs_item) >
			    sf->block_ctx->len) {
leaf_item_out_of_bounce_error:
				pr_info("btrfsic: leaf item out of bounce at logical %llu, dev %s\n",
				       sf->block_ctx->start,
				       sf->block_ctx->dev->name);
				goto one_stack_frame_backwards;
			}
			btrfsic_read_from_block_data(sf->block_ctx,
						     &disk_item,
						     disk_item_offset,
						     sizeof(struct btrfs_item));
			item_offset = btrfs_stack_item_offset(&disk_item);
			item_size = btrfs_stack_item_size(&disk_item);
			disk_key = &disk_item.key;
			type = btrfs_disk_key_type(disk_key);

			if (BTRFS_ROOT_ITEM_KEY == type) {
				struct btrfs_root_item root_item;
				u32 root_item_offset;
				u64 next_bytenr;

				root_item_offset = item_offset +
					offsetof(struct btrfs_leaf, items);
				if (root_item_offset + item_size >
				    sf->block_ctx->len)
					goto leaf_item_out_of_bounce_error;
				btrfsic_read_from_block_data(
					sf->block_ctx, &root_item,
					root_item_offset,
					item_size);
				next_bytenr = btrfs_root_bytenr(&root_item);

				sf->error =
				    btrfsic_create_link_to_next_block(
						state,
						sf->block,
						sf->block_ctx,
						next_bytenr,
						sf->limit_nesting,
						&sf->next_block_ctx,
						&sf->next_block,
						force_iodone_flag,
						&sf->num_copies,
						&sf->mirror_num,
						disk_key,
						btrfs_root_generation(
						&root_item));
				if (sf->error)
					goto one_stack_frame_backwards;

				if (NULL != sf->next_block) {
					struct btrfs_header *const next_hdr =
					    (struct btrfs_header *)
					    sf->next_block_ctx.datav[0];

					next_stack =
					    btrfsic_stack_frame_alloc();
					if (NULL == next_stack) {
						sf->error = -1;
						btrfsic_release_block_ctx(
								&sf->
								next_block_ctx);
						goto one_stack_frame_backwards;
					}

					next_stack->i = -1;
					next_stack->block = sf->next_block;
					next_stack->block_ctx =
					    &sf->next_block_ctx;
					next_stack->next_block = NULL;
					next_stack->hdr = next_hdr;
					next_stack->limit_nesting =
					    sf->limit_nesting - 1;
					next_stack->prev = sf;
					sf = next_stack;
					goto continue_with_new_stack_frame;
				}
			} else if (BTRFS_EXTENT_DATA_KEY == type &&
				   state->include_extent_data) {
				sf->error = btrfsic_handle_extent_data(
						state,
						sf->block,
						sf->block_ctx,
						item_offset,
						force_iodone_flag);
				if (sf->error)
					goto one_stack_frame_backwards;
			}

			goto continue_with_current_leaf_stack_frame;
		}
	} else {
		struct btrfs_node *const nodehdr = (struct btrfs_node *)sf->hdr;

		if (-1 == sf->i) {
			sf->nr = btrfs_stack_header_nritems(&nodehdr->header);

			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("node %llu level %d items %d generation %llu owner %llu\n",
				       sf->block_ctx->start,
				       nodehdr->header.level, sf->nr,
				       btrfs_stack_header_generation(
				       &nodehdr->header),
				       btrfs_stack_header_owner(
				       &nodehdr->header));
		}

continue_with_current_node_stack_frame:
		if (0 == sf->num_copies || sf->mirror_num > sf->num_copies) {
			sf->i++;
			sf->num_copies = 0;
		}

		if (sf->i < sf->nr) {
			struct btrfs_key_ptr key_ptr;
			u32 key_ptr_offset;
			u64 next_bytenr;

			key_ptr_offset = (uintptr_t)(nodehdr->ptrs + sf->i) -
					  (uintptr_t)nodehdr;
			if (key_ptr_offset + sizeof(struct btrfs_key_ptr) >
			    sf->block_ctx->len) {
				pr_info("btrfsic: node item out of bounce at logical %llu, dev %s\n",
				       sf->block_ctx->start,
				       sf->block_ctx->dev->name);
				goto one_stack_frame_backwards;
			}
			btrfsic_read_from_block_data(
				sf->block_ctx, &key_ptr, key_ptr_offset,
				sizeof(struct btrfs_key_ptr));
			next_bytenr = btrfs_stack_key_blockptr(&key_ptr);

			sf->error = btrfsic_create_link_to_next_block(
					state,
					sf->block,
					sf->block_ctx,
					next_bytenr,
					sf->limit_nesting,
					&sf->next_block_ctx,
					&sf->next_block,
					force_iodone_flag,
					&sf->num_copies,
					&sf->mirror_num,
					&key_ptr.key,
					btrfs_stack_key_generation(&key_ptr));
			if (sf->error)
				goto one_stack_frame_backwards;

			if (NULL != sf->next_block) {
				struct btrfs_header *const next_hdr =
				    (struct btrfs_header *)
				    sf->next_block_ctx.datav[0];

				next_stack = btrfsic_stack_frame_alloc();
				if (NULL == next_stack) {
					sf->error = -1;
					goto one_stack_frame_backwards;
				}

				next_stack->i = -1;
				next_stack->block = sf->next_block;
				next_stack->block_ctx = &sf->next_block_ctx;
				next_stack->next_block = NULL;
				next_stack->hdr = next_hdr;
				next_stack->limit_nesting =
				    sf->limit_nesting - 1;
				next_stack->prev = sf;
				sf = next_stack;
				goto continue_with_new_stack_frame;
			}

			goto continue_with_current_node_stack_frame;
		}
	}

one_stack_frame_backwards:
	if (NULL != sf->prev) {
		struct btrfsic_stack_frame *const prev = sf->prev;

		/* the one for the initial block is freed in the caller */
		btrfsic_release_block_ctx(sf->block_ctx);

		if (sf->error) {
			prev->error = sf->error;
			btrfsic_stack_frame_free(sf);
			sf = prev;
			goto one_stack_frame_backwards;
		}

		btrfsic_stack_frame_free(sf);
		sf = prev;
		goto continue_with_new_stack_frame;
	} else {
		BUG_ON(&initial_stack_frame != sf);
	}

	return sf->error;
}

static void btrfsic_read_from_block_data(
	struct btrfsic_block_data_ctx *block_ctx,
	void *dstv, u32 offset, size_t len)
{
	size_t cur;
	size_t offset_in_page;
	char *kaddr;
	char *dst = (char *)dstv;
	size_t start_offset = block_ctx->start & ((u64)PAGE_SIZE - 1);
	unsigned long i = (start_offset + offset) >> PAGE_SHIFT;

	WARN_ON(offset + len > block_ctx->len);
	offset_in_page = (start_offset + offset) & (PAGE_SIZE - 1);

	while (len > 0) {
		cur = min(len, ((size_t)PAGE_SIZE - offset_in_page));
		BUG_ON(i >= DIV_ROUND_UP(block_ctx->len, PAGE_SIZE));
		kaddr = block_ctx->datav[i];
		memcpy(dst, kaddr + offset_in_page, cur);

		dst += cur;
		len -= cur;
		offset_in_page = 0;
		i++;
	}
}

static int btrfsic_create_link_to_next_block(
		struct btrfsic_state *state,
		struct btrfsic_block *block,
		struct btrfsic_block_data_ctx *block_ctx,
		u64 next_bytenr,
		int limit_nesting,
		struct btrfsic_block_data_ctx *next_block_ctx,
		struct btrfsic_block **next_blockp,
		int force_iodone_flag,
		int *num_copiesp, int *mirror_nump,
		struct btrfs_disk_key *disk_key,
		u64 parent_generation)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfsic_block *next_block = NULL;
	int ret;
	struct btrfsic_block_link *l;
	int did_alloc_block_link;
	int block_was_created;

	*next_blockp = NULL;
	if (0 == *num_copiesp) {
		*num_copiesp = btrfs_num_copies(fs_info, next_bytenr,
						state->metablock_size);
		if (state->print_mask & BTRFSIC_PRINT_MASK_NUM_COPIES)
			pr_info("num_copies(log_bytenr=%llu) = %d\n",
			       next_bytenr, *num_copiesp);
		*mirror_nump = 1;
	}

	if (*mirror_nump > *num_copiesp)
		return 0;

	if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
		pr_info("btrfsic_create_link_to_next_block(mirror_num=%d)\n",
		       *mirror_nump);
	ret = btrfsic_map_block(state, next_bytenr,
				state->metablock_size,
				next_block_ctx, *mirror_nump);
	if (ret) {
		pr_info("btrfsic: btrfsic_map_block(@%llu, mirror=%d) failed!\n",
		       next_bytenr, *mirror_nump);
		btrfsic_release_block_ctx(next_block_ctx);
		*next_blockp = NULL;
		return -1;
	}

	next_block = btrfsic_block_lookup_or_add(state,
						 next_block_ctx, "referenced ",
						 1, force_iodone_flag,
						 !force_iodone_flag,
						 *mirror_nump,
						 &block_was_created);
	if (NULL == next_block) {
		btrfsic_release_block_ctx(next_block_ctx);
		*next_blockp = NULL;
		return -1;
	}
	if (block_was_created) {
		l = NULL;
		next_block->generation = BTRFSIC_GENERATION_UNKNOWN;
	} else {
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE) {
			if (next_block->logical_bytenr != next_bytenr &&
			    !(!next_block->is_metadata &&
			      0 == next_block->logical_bytenr))
				pr_info("Referenced block @%llu (%s/%llu/%d) found in hash table, %c, bytenr mismatch (!= stored %llu).\n",
				       next_bytenr, next_block_ctx->dev->name,
				       next_block_ctx->dev_bytenr, *mirror_nump,
				       btrfsic_get_block_type(state,
							      next_block),
				       next_block->logical_bytenr);
			else
				pr_info("Referenced block @%llu (%s/%llu/%d) found in hash table, %c.\n",
				       next_bytenr, next_block_ctx->dev->name,
				       next_block_ctx->dev_bytenr, *mirror_nump,
				       btrfsic_get_block_type(state,
							      next_block));
		}
		next_block->logical_bytenr = next_bytenr;

		next_block->mirror_num = *mirror_nump;
		l = btrfsic_block_link_hashtable_lookup(
				next_block_ctx->dev->bdev,
				next_block_ctx->dev_bytenr,
				block_ctx->dev->bdev,
				block_ctx->dev_bytenr,
				&state->block_link_hashtable);
	}

	next_block->disk_key = *disk_key;
	if (NULL == l) {
		l = btrfsic_block_link_alloc();
		if (NULL == l) {
			pr_info("btrfsic: error, kmalloc failed!\n");
			btrfsic_release_block_ctx(next_block_ctx);
			*next_blockp = NULL;
			return -1;
		}

		did_alloc_block_link = 1;
		l->block_ref_to = next_block;
		l->block_ref_from = block;
		l->ref_cnt = 1;
		l->parent_generation = parent_generation;

		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			btrfsic_print_add_link(state, l);

		list_add(&l->node_ref_to, &block->ref_to_list);
		list_add(&l->node_ref_from, &next_block->ref_from_list);

		btrfsic_block_link_hashtable_add(l,
						 &state->block_link_hashtable);
	} else {
		did_alloc_block_link = 0;
		if (0 == limit_nesting) {
			l->ref_cnt++;
			l->parent_generation = parent_generation;
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				btrfsic_print_add_link(state, l);
		}
	}

	if (limit_nesting > 0 && did_alloc_block_link) {
		ret = btrfsic_read_block(state, next_block_ctx);
		if (ret < (int)next_block_ctx->len) {
			pr_info("btrfsic: read block @logical %llu failed!\n",
			       next_bytenr);
			btrfsic_release_block_ctx(next_block_ctx);
			*next_blockp = NULL;
			return -1;
		}

		*next_blockp = next_block;
	} else {
		*next_blockp = NULL;
	}
	(*mirror_nump)++;

	return 0;
}

static int btrfsic_handle_extent_data(
		struct btrfsic_state *state,
		struct btrfsic_block *block,
		struct btrfsic_block_data_ctx *block_ctx,
		u32 item_offset, int force_iodone_flag)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfs_file_extent_item file_extent_item;
	u64 file_extent_item_offset;
	u64 next_bytenr;
	u64 num_bytes;
	u64 generation;
	struct btrfsic_block_link *l;
	int ret;

	file_extent_item_offset = offsetof(struct btrfs_leaf, items) +
				  item_offset;
	if (file_extent_item_offset +
	    offsetof(struct btrfs_file_extent_item, disk_num_bytes) >
	    block_ctx->len) {
		pr_info("btrfsic: file item out of bounce at logical %llu, dev %s\n",
		       block_ctx->start, block_ctx->dev->name);
		return -1;
	}

	btrfsic_read_from_block_data(block_ctx, &file_extent_item,
		file_extent_item_offset,
		offsetof(struct btrfs_file_extent_item, disk_num_bytes));
	if (BTRFS_FILE_EXTENT_REG != file_extent_item.type ||
	    btrfs_stack_file_extent_disk_bytenr(&file_extent_item) == 0) {
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERY_VERBOSE)
			pr_info("extent_data: type %u, disk_bytenr = %llu\n",
			       file_extent_item.type,
			       btrfs_stack_file_extent_disk_bytenr(
			       &file_extent_item));
		return 0;
	}

	if (file_extent_item_offset + sizeof(struct btrfs_file_extent_item) >
	    block_ctx->len) {
		pr_info("btrfsic: file item out of bounce at logical %llu, dev %s\n",
		       block_ctx->start, block_ctx->dev->name);
		return -1;
	}
	btrfsic_read_from_block_data(block_ctx, &file_extent_item,
				     file_extent_item_offset,
				     sizeof(struct btrfs_file_extent_item));
	next_bytenr = btrfs_stack_file_extent_disk_bytenr(&file_extent_item);
	if (btrfs_stack_file_extent_compression(&file_extent_item) ==
	    BTRFS_COMPRESS_NONE) {
		next_bytenr += btrfs_stack_file_extent_offset(&file_extent_item);
		num_bytes = btrfs_stack_file_extent_num_bytes(&file_extent_item);
	} else {
		num_bytes = btrfs_stack_file_extent_disk_num_bytes(&file_extent_item);
	}
	generation = btrfs_stack_file_extent_generation(&file_extent_item);

	if (state->print_mask & BTRFSIC_PRINT_MASK_VERY_VERBOSE)
		pr_info("extent_data: type %u, disk_bytenr = %llu, offset = %llu, num_bytes = %llu\n",
		       file_extent_item.type,
		       btrfs_stack_file_extent_disk_bytenr(&file_extent_item),
		       btrfs_stack_file_extent_offset(&file_extent_item),
		       num_bytes);
	while (num_bytes > 0) {
		u32 chunk_len;
		int num_copies;
		int mirror_num;

		if (num_bytes > state->datablock_size)
			chunk_len = state->datablock_size;
		else
			chunk_len = num_bytes;

		num_copies = btrfs_num_copies(fs_info, next_bytenr,
					      state->datablock_size);
		if (state->print_mask & BTRFSIC_PRINT_MASK_NUM_COPIES)
			pr_info("num_copies(log_bytenr=%llu) = %d\n",
			       next_bytenr, num_copies);
		for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
			struct btrfsic_block_data_ctx next_block_ctx;
			struct btrfsic_block *next_block;
			int block_was_created;

			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("btrfsic_handle_extent_data(mirror_num=%d)\n",
					mirror_num);
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERY_VERBOSE)
				pr_info("\tdisk_bytenr = %llu, num_bytes %u\n",
				       next_bytenr, chunk_len);
			ret = btrfsic_map_block(state, next_bytenr,
						chunk_len, &next_block_ctx,
						mirror_num);
			if (ret) {
				pr_info("btrfsic: btrfsic_map_block(@%llu, mirror=%d) failed!\n",
				       next_bytenr, mirror_num);
				return -1;
			}

			next_block = btrfsic_block_lookup_or_add(
					state,
					&next_block_ctx,
					"referenced ",
					0,
					force_iodone_flag,
					!force_iodone_flag,
					mirror_num,
					&block_was_created);
			if (NULL == next_block) {
				pr_info("btrfsic: error, kmalloc failed!\n");
				btrfsic_release_block_ctx(&next_block_ctx);
				return -1;
			}
			if (!block_was_created) {
				if ((state->print_mask &
				     BTRFSIC_PRINT_MASK_VERBOSE) &&
				    next_block->logical_bytenr != next_bytenr &&
				    !(!next_block->is_metadata &&
				      0 == next_block->logical_bytenr)) {
					pr_info("Referenced block @%llu (%s/%llu/%d) found in hash table, D, bytenr mismatch (!= stored %llu).\n",
					       next_bytenr,
					       next_block_ctx.dev->name,
					       next_block_ctx.dev_bytenr,
					       mirror_num,
					       next_block->logical_bytenr);
				}
				next_block->logical_bytenr = next_bytenr;
				next_block->mirror_num = mirror_num;
			}

			l = btrfsic_block_link_lookup_or_add(state,
							     &next_block_ctx,
							     next_block, block,
							     generation);
			btrfsic_release_block_ctx(&next_block_ctx);
			if (NULL == l)
				return -1;
		}

		next_bytenr += chunk_len;
		num_bytes -= chunk_len;
	}

	return 0;
}

static int btrfsic_map_block(struct btrfsic_state *state, u64 bytenr, u32 len,
			     struct btrfsic_block_data_ctx *block_ctx_out,
			     int mirror_num)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	int ret;
	u64 length;
	struct btrfs_bio *multi = NULL;
	struct btrfs_device *device;

	length = len;
	ret = btrfs_map_block(fs_info, BTRFS_MAP_READ,
			      bytenr, &length, &multi, mirror_num);

	if (ret) {
		block_ctx_out->start = 0;
		block_ctx_out->dev_bytenr = 0;
		block_ctx_out->len = 0;
		block_ctx_out->dev = NULL;
		block_ctx_out->datav = NULL;
		block_ctx_out->pagev = NULL;
		block_ctx_out->mem_to_free = NULL;

		return ret;
	}

	device = multi->stripes[0].dev;
	if (test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state) ||
	    !device->bdev || !device->name)
		block_ctx_out->dev = NULL;
	else
		block_ctx_out->dev = btrfsic_dev_state_lookup(
							device->bdev->bd_dev);
	block_ctx_out->dev_bytenr = multi->stripes[0].physical;
	block_ctx_out->start = bytenr;
	block_ctx_out->len = len;
	block_ctx_out->datav = NULL;
	block_ctx_out->pagev = NULL;
	block_ctx_out->mem_to_free = NULL;

	kfree(multi);
	if (NULL == block_ctx_out->dev) {
		ret = -ENXIO;
		pr_info("btrfsic: error, cannot lookup dev (#1)!\n");
	}

	return ret;
}

static void btrfsic_release_block_ctx(struct btrfsic_block_data_ctx *block_ctx)
{
	if (block_ctx->mem_to_free) {
		unsigned int num_pages;

		BUG_ON(!block_ctx->datav);
		BUG_ON(!block_ctx->pagev);
		num_pages = (block_ctx->len + (u64)PAGE_SIZE - 1) >>
			    PAGE_SHIFT;
		while (num_pages > 0) {
			num_pages--;
			if (block_ctx->datav[num_pages]) {
				kunmap(block_ctx->pagev[num_pages]);
				block_ctx->datav[num_pages] = NULL;
			}
			if (block_ctx->pagev[num_pages]) {
				__free_page(block_ctx->pagev[num_pages]);
				block_ctx->pagev[num_pages] = NULL;
			}
		}

		kfree(block_ctx->mem_to_free);
		block_ctx->mem_to_free = NULL;
		block_ctx->pagev = NULL;
		block_ctx->datav = NULL;
	}
}

static int btrfsic_read_block(struct btrfsic_state *state,
			      struct btrfsic_block_data_ctx *block_ctx)
{
	unsigned int num_pages;
	unsigned int i;
	size_t size;
	u64 dev_bytenr;
	int ret;

	BUG_ON(block_ctx->datav);
	BUG_ON(block_ctx->pagev);
	BUG_ON(block_ctx->mem_to_free);
	if (block_ctx->dev_bytenr & ((u64)PAGE_SIZE - 1)) {
		pr_info("btrfsic: read_block() with unaligned bytenr %llu\n",
		       block_ctx->dev_bytenr);
		return -1;
	}

	num_pages = (block_ctx->len + (u64)PAGE_SIZE - 1) >>
		    PAGE_SHIFT;
	size = sizeof(*block_ctx->datav) + sizeof(*block_ctx->pagev);
	block_ctx->mem_to_free = kcalloc(num_pages, size, GFP_NOFS);
	if (!block_ctx->mem_to_free)
		return -ENOMEM;
	block_ctx->datav = block_ctx->mem_to_free;
	block_ctx->pagev = (struct page **)(block_ctx->datav + num_pages);
	for (i = 0; i < num_pages; i++) {
		block_ctx->pagev[i] = alloc_page(GFP_NOFS);
		if (!block_ctx->pagev[i])
			return -1;
	}

	dev_bytenr = block_ctx->dev_bytenr;
	for (i = 0; i < num_pages;) {
		struct bio *bio;
		unsigned int j;

		bio = btrfs_io_bio_alloc(num_pages - i);
		bio_set_dev(bio, block_ctx->dev->bdev);
		bio->bi_iter.bi_sector = dev_bytenr >> 9;
		bio->bi_opf = REQ_OP_READ;

		for (j = i; j < num_pages; j++) {
			ret = bio_add_page(bio, block_ctx->pagev[j],
					   PAGE_SIZE, 0);
			if (PAGE_SIZE != ret)
				break;
		}
		if (j == i) {
			pr_info("btrfsic: error, failed to add a single page!\n");
			return -1;
		}
		if (submit_bio_wait(bio)) {
			pr_info("btrfsic: read error at logical %llu dev %s!\n",
			       block_ctx->start, block_ctx->dev->name);
			bio_put(bio);
			return -1;
		}
		bio_put(bio);
		dev_bytenr += (j - i) * PAGE_SIZE;
		i = j;
	}
	for (i = 0; i < num_pages; i++)
		block_ctx->datav[i] = kmap(block_ctx->pagev[i]);

	return block_ctx->len;
}

static void btrfsic_dump_database(struct btrfsic_state *state)
{
	const struct btrfsic_block *b_all;

	BUG_ON(NULL == state);

	pr_info("all_blocks_list:\n");
	list_for_each_entry(b_all, &state->all_blocks_list, all_blocks_node) {
		const struct btrfsic_block_link *l;

		pr_info("%c-block @%llu (%s/%llu/%d)\n",
		       btrfsic_get_block_type(state, b_all),
		       b_all->logical_bytenr, b_all->dev_state->name,
		       b_all->dev_bytenr, b_all->mirror_num);

		list_for_each_entry(l, &b_all->ref_to_list, node_ref_to) {
			pr_info(" %c @%llu (%s/%llu/%d) refers %u* to %c @%llu (%s/%llu/%d)\n",
			       btrfsic_get_block_type(state, b_all),
			       b_all->logical_bytenr, b_all->dev_state->name,
			       b_all->dev_bytenr, b_all->mirror_num,
			       l->ref_cnt,
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num);
		}

		list_for_each_entry(l, &b_all->ref_from_list, node_ref_from) {
			pr_info(" %c @%llu (%s/%llu/%d) is ref %u* from %c @%llu (%s/%llu/%d)\n",
			       btrfsic_get_block_type(state, b_all),
			       b_all->logical_bytenr, b_all->dev_state->name,
			       b_all->dev_bytenr, b_all->mirror_num,
			       l->ref_cnt,
			       btrfsic_get_block_type(state, l->block_ref_from),
			       l->block_ref_from->logical_bytenr,
			       l->block_ref_from->dev_state->name,
			       l->block_ref_from->dev_bytenr,
			       l->block_ref_from->mirror_num);
		}

		pr_info("\n");
	}
}

/*
 * Test whether the disk block contains a tree block (leaf or node)
 * (note that this test fails for the super block)
 */
static int btrfsic_test_for_metadata(struct btrfsic_state *state,
				     char **datav, unsigned int num_pages)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfs_header *h;
	u8 csum[BTRFS_CSUM_SIZE];
	u32 crc = ~(u32)0;
	unsigned int i;

	if (num_pages * PAGE_SIZE < state->metablock_size)
		return 1; /* not metadata */
	num_pages = state->metablock_size >> PAGE_SHIFT;
	h = (struct btrfs_header *)datav[0];

	if (memcmp(h->fsid, fs_info->fs_devices->fsid, BTRFS_FSID_SIZE))
		return 1;

	for (i = 0; i < num_pages; i++) {
		u8 *data = i ? datav[i] : (datav[i] + BTRFS_CSUM_SIZE);
		size_t sublen = i ? PAGE_SIZE :
				    (PAGE_SIZE - BTRFS_CSUM_SIZE);

		crc = crc32c(crc, data, sublen);
	}
	btrfs_csum_final(crc, csum);
	if (memcmp(csum, h->csum, state->csum_size))
		return 1;

	return 0; /* is metadata */
}

static void btrfsic_process_written_block(struct btrfsic_dev_state *dev_state,
					  u64 dev_bytenr, char **mapped_datav,
					  unsigned int num_pages,
					  struct bio *bio, int *bio_is_patched,
					  struct buffer_head *bh,
					  int submit_bio_bh_rw)
{
	int is_metadata;
	struct btrfsic_block *block;
	struct btrfsic_block_data_ctx block_ctx;
	int ret;
	struct btrfsic_state *state = dev_state->state;
	struct block_device *bdev = dev_state->bdev;
	unsigned int processed_len;

	if (NULL != bio_is_patched)
		*bio_is_patched = 0;

again:
	if (num_pages == 0)
		return;

	processed_len = 0;
	is_metadata = (0 == btrfsic_test_for_metadata(state, mapped_datav,
						      num_pages));

	block = btrfsic_block_hashtable_lookup(bdev, dev_bytenr,
					       &state->block_hashtable);
	if (NULL != block) {
		u64 bytenr = 0;
		struct btrfsic_block_link *l, *tmp;

		if (block->is_superblock) {
			bytenr = btrfs_super_bytenr((struct btrfs_super_block *)
						    mapped_datav[0]);
			if (num_pages * PAGE_SIZE <
			    BTRFS_SUPER_INFO_SIZE) {
				pr_info("btrfsic: cannot work with too short bios!\n");
				return;
			}
			is_metadata = 1;
			BUG_ON(BTRFS_SUPER_INFO_SIZE & (PAGE_SIZE - 1));
			processed_len = BTRFS_SUPER_INFO_SIZE;
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_TREE_BEFORE_SB_WRITE) {
				pr_info("[before new superblock is written]:\n");
				btrfsic_dump_tree_sub(state, block, 0);
			}
		}
		if (is_metadata) {
			if (!block->is_superblock) {
				if (num_pages * PAGE_SIZE <
				    state->metablock_size) {
					pr_info("btrfsic: cannot work with too short bios!\n");
					return;
				}
				processed_len = state->metablock_size;
				bytenr = btrfs_stack_header_bytenr(
						(struct btrfs_header *)
						mapped_datav[0]);
				btrfsic_cmp_log_and_dev_bytenr(state, bytenr,
							       dev_state,
							       dev_bytenr);
			}
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE) {
				if (block->logical_bytenr != bytenr &&
				    !(!block->is_metadata &&
				      block->logical_bytenr == 0))
					pr_info("Written block @%llu (%s/%llu/%d) found in hash table, %c, bytenr mismatch (!= stored %llu).\n",
					       bytenr, dev_state->name,
					       dev_bytenr,
					       block->mirror_num,
					       btrfsic_get_block_type(state,
								      block),
					       block->logical_bytenr);
				else
					pr_info("Written block @%llu (%s/%llu/%d) found in hash table, %c.\n",
					       bytenr, dev_state->name,
					       dev_bytenr, block->mirror_num,
					       btrfsic_get_block_type(state,
								      block));
			}
			block->logical_bytenr = bytenr;
		} else {
			if (num_pages * PAGE_SIZE <
			    state->datablock_size) {
				pr_info("btrfsic: cannot work with too short bios!\n");
				return;
			}
			processed_len = state->datablock_size;
			bytenr = block->logical_bytenr;
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("Written block @%llu (%s/%llu/%d) found in hash table, %c.\n",
				       bytenr, dev_state->name, dev_bytenr,
				       block->mirror_num,
				       btrfsic_get_block_type(state, block));
		}

		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("ref_to_list: %cE, ref_from_list: %cE\n",
			       list_empty(&block->ref_to_list) ? ' ' : '!',
			       list_empty(&block->ref_from_list) ? ' ' : '!');
		if (btrfsic_is_block_ref_by_superblock(state, block, 0)) {
			pr_info("btrfs: attempt to overwrite %c-block @%llu (%s/%llu/%d), old(gen=%llu, objectid=%llu, type=%d, offset=%llu), new(gen=%llu), which is referenced by most recent superblock (superblockgen=%llu)!\n",
			       btrfsic_get_block_type(state, block), bytenr,
			       dev_state->name, dev_bytenr, block->mirror_num,
			       block->generation,
			       btrfs_disk_key_objectid(&block->disk_key),
			       block->disk_key.type,
			       btrfs_disk_key_offset(&block->disk_key),
			       btrfs_stack_header_generation(
				       (struct btrfs_header *) mapped_datav[0]),
			       state->max_superblock_generation);
			btrfsic_dump_tree(state);
		}

		if (!block->is_iodone && !block->never_written) {
			pr_info("btrfs: attempt to overwrite %c-block @%llu (%s/%llu/%d), oldgen=%llu, newgen=%llu, which is not yet iodone!\n",
			       btrfsic_get_block_type(state, block), bytenr,
			       dev_state->name, dev_bytenr, block->mirror_num,
			       block->generation,
			       btrfs_stack_header_generation(
				       (struct btrfs_header *)
				       mapped_datav[0]));
			/* it would not be safe to go on */
			btrfsic_dump_tree(state);
			goto continue_loop;
		}

		/*
		 * Clear all references of this block. Do not free
		 * the block itself even if is not referenced anymore
		 * because it still carries valuable information
		 * like whether it was ever written and IO completed.
		 */
		list_for_each_entry_safe(l, tmp, &block->ref_to_list,
					 node_ref_to) {
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				btrfsic_print_rem_link(state, l);
			l->ref_cnt--;
			if (0 == l->ref_cnt) {
				list_del(&l->node_ref_to);
				list_del(&l->node_ref_from);
				btrfsic_block_link_hashtable_remove(l);
				btrfsic_block_link_free(l);
			}
		}

		block_ctx.dev = dev_state;
		block_ctx.dev_bytenr = dev_bytenr;
		block_ctx.start = bytenr;
		block_ctx.len = processed_len;
		block_ctx.pagev = NULL;
		block_ctx.mem_to_free = NULL;
		block_ctx.datav = mapped_datav;

		if (is_metadata || state->include_extent_data) {
			block->never_written = 0;
			block->iodone_w_error = 0;
			if (NULL != bio) {
				block->is_iodone = 0;
				BUG_ON(NULL == bio_is_patched);
				if (!*bio_is_patched) {
					block->orig_bio_bh_private =
					    bio->bi_private;
					block->orig_bio_bh_end_io.bio =
					    bio->bi_end_io;
					block->next_in_same_bio = NULL;
					bio->bi_private = block;
					bio->bi_end_io = btrfsic_bio_end_io;
					*bio_is_patched = 1;
				} else {
					struct btrfsic_block *chained_block =
					    (struct btrfsic_block *)
					    bio->bi_private;

					BUG_ON(NULL == chained_block);
					block->orig_bio_bh_private =
					    chained_block->orig_bio_bh_private;
					block->orig_bio_bh_end_io.bio =
					    chained_block->orig_bio_bh_end_io.
					    bio;
					block->next_in_same_bio = chained_block;
					bio->bi_private = block;
				}
			} else if (NULL != bh) {
				block->is_iodone = 0;
				block->orig_bio_bh_private = bh->b_private;
				block->orig_bio_bh_end_io.bh = bh->b_end_io;
				block->next_in_same_bio = NULL;
				bh->b_private = block;
				bh->b_end_io = btrfsic_bh_end_io;
			} else {
				block->is_iodone = 1;
				block->orig_bio_bh_private = NULL;
				block->orig_bio_bh_end_io.bio = NULL;
				block->next_in_same_bio = NULL;
			}
		}

		block->flush_gen = dev_state->last_flush_gen + 1;
		block->submit_bio_bh_rw = submit_bio_bh_rw;
		if (is_metadata) {
			block->logical_bytenr = bytenr;
			block->is_metadata = 1;
			if (block->is_superblock) {
				BUG_ON(PAGE_SIZE !=
				       BTRFS_SUPER_INFO_SIZE);
				ret = btrfsic_process_written_superblock(
						state,
						block,
						(struct btrfs_super_block *)
						mapped_datav[0]);
				if (state->print_mask &
				    BTRFSIC_PRINT_MASK_TREE_AFTER_SB_WRITE) {
					pr_info("[after new superblock is written]:\n");
					btrfsic_dump_tree_sub(state, block, 0);
				}
			} else {
				block->mirror_num = 0;	/* unknown */
				ret = btrfsic_process_metablock(
						state,
						block,
						&block_ctx,
						0, 0);
			}
			if (ret)
				pr_info("btrfsic: btrfsic_process_metablock(root @%llu) failed!\n",
				       dev_bytenr);
		} else {
			block->is_metadata = 0;
			block->mirror_num = 0;	/* unknown */
			block->generation = BTRFSIC_GENERATION_UNKNOWN;
			if (!state->include_extent_data
			    && list_empty(&block->ref_from_list)) {
				/*
				 * disk block is overwritten with extent
				 * data (not meta data) and we are configured
				 * to not include extent data: take the
				 * chance and free the block's memory
				 */
				btrfsic_block_hashtable_remove(block);
				list_del(&block->all_blocks_node);
				btrfsic_block_free(block);
			}
		}
		btrfsic_release_block_ctx(&block_ctx);
	} else {
		/* block has not been found in hash table */
		u64 bytenr;

		if (!is_metadata) {
			processed_len = state->datablock_size;
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("Written block (%s/%llu/?) !found in hash table, D.\n",
				       dev_state->name, dev_bytenr);
			if (!state->include_extent_data) {
				/* ignore that written D block */
				goto continue_loop;
			}

			/* this is getting ugly for the
			 * include_extent_data case... */
			bytenr = 0;	/* unknown */
		} else {
			processed_len = state->metablock_size;
			bytenr = btrfs_stack_header_bytenr(
					(struct btrfs_header *)
					mapped_datav[0]);
			btrfsic_cmp_log_and_dev_bytenr(state, bytenr, dev_state,
						       dev_bytenr);
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("Written block @%llu (%s/%llu/?) !found in hash table, M.\n",
				       bytenr, dev_state->name, dev_bytenr);
		}

		block_ctx.dev = dev_state;
		block_ctx.dev_bytenr = dev_bytenr;
		block_ctx.start = bytenr;
		block_ctx.len = processed_len;
		block_ctx.pagev = NULL;
		block_ctx.mem_to_free = NULL;
		block_ctx.datav = mapped_datav;

		block = btrfsic_block_alloc();
		if (NULL == block) {
			pr_info("btrfsic: error, kmalloc failed!\n");
			btrfsic_release_block_ctx(&block_ctx);
			goto continue_loop;
		}
		block->dev_state = dev_state;
		block->dev_bytenr = dev_bytenr;
		block->logical_bytenr = bytenr;
		block->is_metadata = is_metadata;
		block->never_written = 0;
		block->iodone_w_error = 0;
		block->mirror_num = 0;	/* unknown */
		block->flush_gen = dev_state->last_flush_gen + 1;
		block->submit_bio_bh_rw = submit_bio_bh_rw;
		if (NULL != bio) {
			block->is_iodone = 0;
			BUG_ON(NULL == bio_is_patched);
			if (!*bio_is_patched) {
				block->orig_bio_bh_private = bio->bi_private;
				block->orig_bio_bh_end_io.bio = bio->bi_end_io;
				block->next_in_same_bio = NULL;
				bio->bi_private = block;
				bio->bi_end_io = btrfsic_bio_end_io;
				*bio_is_patched = 1;
			} else {
				struct btrfsic_block *chained_block =
				    (struct btrfsic_block *)
				    bio->bi_private;

				BUG_ON(NULL == chained_block);
				block->orig_bio_bh_private =
				    chained_block->orig_bio_bh_private;
				block->orig_bio_bh_end_io.bio =
				    chained_block->orig_bio_bh_end_io.bio;
				block->next_in_same_bio = chained_block;
				bio->bi_private = block;
			}
		} else if (NULL != bh) {
			block->is_iodone = 0;
			block->orig_bio_bh_private = bh->b_private;
			block->orig_bio_bh_end_io.bh = bh->b_end_io;
			block->next_in_same_bio = NULL;
			bh->b_private = block;
			bh->b_end_io = btrfsic_bh_end_io;
		} else {
			block->is_iodone = 1;
			block->orig_bio_bh_private = NULL;
			block->orig_bio_bh_end_io.bio = NULL;
			block->next_in_same_bio = NULL;
		}
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("New written %c-block @%llu (%s/%llu/%d)\n",
			       is_metadata ? 'M' : 'D',
			       block->logical_bytenr, block->dev_state->name,
			       block->dev_bytenr, block->mirror_num);
		list_add(&block->all_blocks_node, &state->all_blocks_list);
		btrfsic_block_hashtable_add(block, &state->block_hashtable);

		if (is_metadata) {
			ret = btrfsic_process_metablock(state, block,
							&block_ctx, 0, 0);
			if (ret)
				pr_info("btrfsic: process_metablock(root @%llu) failed!\n",
				       dev_bytenr);
		}
		btrfsic_release_block_ctx(&block_ctx);
	}

continue_loop:
	BUG_ON(!processed_len);
	dev_bytenr += processed_len;
	mapped_datav += processed_len >> PAGE_SHIFT;
	num_pages -= processed_len >> PAGE_SHIFT;
	goto again;
}

static void btrfsic_bio_end_io(struct bio *bp)
{
	struct btrfsic_block *block = (struct btrfsic_block *)bp->bi_private;
	int iodone_w_error;

	/* mutex is not held! This is not save if IO is not yet completed
	 * on umount */
	iodone_w_error = 0;
	if (bp->bi_status)
		iodone_w_error = 1;

	BUG_ON(NULL == block);
	bp->bi_private = block->orig_bio_bh_private;
	bp->bi_end_io = block->orig_bio_bh_end_io.bio;

	do {
		struct btrfsic_block *next_block;
		struct btrfsic_dev_state *const dev_state = block->dev_state;

		if ((dev_state->state->print_mask &
		     BTRFSIC_PRINT_MASK_END_IO_BIO_BH))
			pr_info("bio_end_io(err=%d) for %c @%llu (%s/%llu/%d)\n",
			       bp->bi_status,
			       btrfsic_get_block_type(dev_state->state, block),
			       block->logical_bytenr, dev_state->name,
			       block->dev_bytenr, block->mirror_num);
		next_block = block->next_in_same_bio;
		block->iodone_w_error = iodone_w_error;
		if (block->submit_bio_bh_rw & REQ_PREFLUSH) {
			dev_state->last_flush_gen++;
			if ((dev_state->state->print_mask &
			     BTRFSIC_PRINT_MASK_END_IO_BIO_BH))
				pr_info("bio_end_io() new %s flush_gen=%llu\n",
				       dev_state->name,
				       dev_state->last_flush_gen);
		}
		if (block->submit_bio_bh_rw & REQ_FUA)
			block->flush_gen = 0; /* FUA completed means block is
					       * on disk */
		block->is_iodone = 1; /* for FLUSH, this releases the block */
		block = next_block;
	} while (NULL != block);

	bp->bi_end_io(bp);
}

static void btrfsic_bh_end_io(struct buffer_head *bh, int uptodate)
{
	struct btrfsic_block *block = (struct btrfsic_block *)bh->b_private;
	int iodone_w_error = !uptodate;
	struct btrfsic_dev_state *dev_state;

	BUG_ON(NULL == block);
	dev_state = block->dev_state;
	if ((dev_state->state->print_mask & BTRFSIC_PRINT_MASK_END_IO_BIO_BH))
		pr_info("bh_end_io(error=%d) for %c @%llu (%s/%llu/%d)\n",
		       iodone_w_error,
		       btrfsic_get_block_type(dev_state->state, block),
		       block->logical_bytenr, block->dev_state->name,
		       block->dev_bytenr, block->mirror_num);

	block->iodone_w_error = iodone_w_error;
	if (block->submit_bio_bh_rw & REQ_PREFLUSH) {
		dev_state->last_flush_gen++;
		if ((dev_state->state->print_mask &
		     BTRFSIC_PRINT_MASK_END_IO_BIO_BH))
			pr_info("bh_end_io() new %s flush_gen=%llu\n",
			       dev_state->name, dev_state->last_flush_gen);
	}
	if (block->submit_bio_bh_rw & REQ_FUA)
		block->flush_gen = 0; /* FUA completed means block is on disk */

	bh->b_private = block->orig_bio_bh_private;
	bh->b_end_io = block->orig_bio_bh_end_io.bh;
	block->is_iodone = 1; /* for FLUSH, this releases the block */
	bh->b_end_io(bh, uptodate);
}

static int btrfsic_process_written_superblock(
		struct btrfsic_state *state,
		struct btrfsic_block *const superblock,
		struct btrfs_super_block *const super_hdr)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	int pass;

	superblock->generation = btrfs_super_generation(super_hdr);
	if (!(superblock->generation > state->max_superblock_generation ||
	      0 == state->max_superblock_generation)) {
		if (state->print_mask & BTRFSIC_PRINT_MASK_SUPERBLOCK_WRITE)
			pr_info("btrfsic: superblock @%llu (%s/%llu/%d) with old gen %llu <= %llu\n",
			       superblock->logical_bytenr,
			       superblock->dev_state->name,
			       superblock->dev_bytenr, superblock->mirror_num,
			       btrfs_super_generation(super_hdr),
			       state->max_superblock_generation);
	} else {
		if (state->print_mask & BTRFSIC_PRINT_MASK_SUPERBLOCK_WRITE)
			pr_info("btrfsic: got new superblock @%llu (%s/%llu/%d) with new gen %llu > %llu\n",
			       superblock->logical_bytenr,
			       superblock->dev_state->name,
			       superblock->dev_bytenr, superblock->mirror_num,
			       btrfs_super_generation(super_hdr),
			       state->max_superblock_generation);

		state->max_superblock_generation =
		    btrfs_super_generation(super_hdr);
		state->latest_superblock = superblock;
	}

	for (pass = 0; pass < 3; pass++) {
		int ret;
		u64 next_bytenr;
		struct btrfsic_block *next_block;
		struct btrfsic_block_data_ctx tmp_next_block_ctx;
		struct btrfsic_block_link *l;
		int num_copies;
		int mirror_num;
		const char *additional_string = NULL;
		struct btrfs_disk_key tmp_disk_key = {0};

		btrfs_set_disk_key_objectid(&tmp_disk_key,
					    BTRFS_ROOT_ITEM_KEY);
		btrfs_set_disk_key_objectid(&tmp_disk_key, 0);

		switch (pass) {
		case 0:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_ROOT_TREE_OBJECTID);
			additional_string = "root ";
			next_bytenr = btrfs_super_root(super_hdr);
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("root@%llu\n", next_bytenr);
			break;
		case 1:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_CHUNK_TREE_OBJECTID);
			additional_string = "chunk ";
			next_bytenr = btrfs_super_chunk_root(super_hdr);
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("chunk@%llu\n", next_bytenr);
			break;
		case 2:
			btrfs_set_disk_key_objectid(&tmp_disk_key,
						    BTRFS_TREE_LOG_OBJECTID);
			additional_string = "log ";
			next_bytenr = btrfs_super_log_root(super_hdr);
			if (0 == next_bytenr)
				continue;
			if (state->print_mask &
			    BTRFSIC_PRINT_MASK_ROOT_CHUNK_LOG_TREE_LOCATION)
				pr_info("log@%llu\n", next_bytenr);
			break;
		}

		num_copies = btrfs_num_copies(fs_info, next_bytenr,
					      BTRFS_SUPER_INFO_SIZE);
		if (state->print_mask & BTRFSIC_PRINT_MASK_NUM_COPIES)
			pr_info("num_copies(log_bytenr=%llu) = %d\n",
			       next_bytenr, num_copies);
		for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
			int was_created;

			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				pr_info("btrfsic_process_written_superblock(mirror_num=%d)\n", mirror_num);
			ret = btrfsic_map_block(state, next_bytenr,
						BTRFS_SUPER_INFO_SIZE,
						&tmp_next_block_ctx,
						mirror_num);
			if (ret) {
				pr_info("btrfsic: btrfsic_map_block(@%llu, mirror=%d) failed!\n",
				       next_bytenr, mirror_num);
				return -1;
			}

			next_block = btrfsic_block_lookup_or_add(
					state,
					&tmp_next_block_ctx,
					additional_string,
					1, 0, 1,
					mirror_num,
					&was_created);
			if (NULL == next_block) {
				pr_info("btrfsic: error, kmalloc failed!\n");
				btrfsic_release_block_ctx(&tmp_next_block_ctx);
				return -1;
			}

			next_block->disk_key = tmp_disk_key;
			if (was_created)
				next_block->generation =
				    BTRFSIC_GENERATION_UNKNOWN;
			l = btrfsic_block_link_lookup_or_add(
					state,
					&tmp_next_block_ctx,
					next_block,
					superblock,
					BTRFSIC_GENERATION_UNKNOWN);
			btrfsic_release_block_ctx(&tmp_next_block_ctx);
			if (NULL == l)
				return -1;
		}
	}

	if (WARN_ON(-1 == btrfsic_check_all_ref_blocks(state, superblock, 0)))
		btrfsic_dump_tree(state);

	return 0;
}

static int btrfsic_check_all_ref_blocks(struct btrfsic_state *state,
					struct btrfsic_block *const block,
					int recursion_level)
{
	const struct btrfsic_block_link *l;
	int ret = 0;

	if (recursion_level >= 3 + BTRFS_MAX_LEVEL) {
		/*
		 * Note that this situation can happen and does not
		 * indicate an error in regular cases. It happens
		 * when disk blocks are freed and later reused.
		 * The check-integrity module is not aware of any
		 * block free operations, it just recognizes block
		 * write operations. Therefore it keeps the linkage
		 * information for a block until a block is
		 * rewritten. This can temporarily cause incorrect
		 * and even circular linkage informations. This
		 * causes no harm unless such blocks are referenced
		 * by the most recent super block.
		 */
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("btrfsic: abort cyclic linkage (case 1).\n");

		return ret;
	}

	/*
	 * This algorithm is recursive because the amount of used stack
	 * space is very small and the max recursion depth is limited.
	 */
	list_for_each_entry(l, &block->ref_to_list, node_ref_to) {
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("rl=%d, %c @%llu (%s/%llu/%d) %u* refers to %c @%llu (%s/%llu/%d)\n",
			       recursion_level,
			       btrfsic_get_block_type(state, block),
			       block->logical_bytenr, block->dev_state->name,
			       block->dev_bytenr, block->mirror_num,
			       l->ref_cnt,
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num);
		if (l->block_ref_to->never_written) {
			pr_info("btrfs: attempt to write superblock which references block %c @%llu (%s/%llu/%d) which is never written!\n",
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num);
			ret = -1;
		} else if (!l->block_ref_to->is_iodone) {
			pr_info("btrfs: attempt to write superblock which references block %c @%llu (%s/%llu/%d) which is not yet iodone!\n",
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num);
			ret = -1;
		} else if (l->block_ref_to->iodone_w_error) {
			pr_info("btrfs: attempt to write superblock which references block %c @%llu (%s/%llu/%d) which has write error!\n",
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num);
			ret = -1;
		} else if (l->parent_generation !=
			   l->block_ref_to->generation &&
			   BTRFSIC_GENERATION_UNKNOWN !=
			   l->parent_generation &&
			   BTRFSIC_GENERATION_UNKNOWN !=
			   l->block_ref_to->generation) {
			pr_info("btrfs: attempt to write superblock which references block %c @%llu (%s/%llu/%d) with generation %llu != parent generation %llu!\n",
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num,
			       l->block_ref_to->generation,
			       l->parent_generation);
			ret = -1;
		} else if (l->block_ref_to->flush_gen >
			   l->block_ref_to->dev_state->last_flush_gen) {
			pr_info("btrfs: attempt to write superblock which references block %c @%llu (%s/%llu/%d) which is not flushed out of disk's write cache (block flush_gen=%llu, dev->flush_gen=%llu)!\n",
			       btrfsic_get_block_type(state, l->block_ref_to),
			       l->block_ref_to->logical_bytenr,
			       l->block_ref_to->dev_state->name,
			       l->block_ref_to->dev_bytenr,
			       l->block_ref_to->mirror_num, block->flush_gen,
			       l->block_ref_to->dev_state->last_flush_gen);
			ret = -1;
		} else if (-1 == btrfsic_check_all_ref_blocks(state,
							      l->block_ref_to,
							      recursion_level +
							      1)) {
			ret = -1;
		}
	}

	return ret;
}

static int btrfsic_is_block_ref_by_superblock(
		const struct btrfsic_state *state,
		const struct btrfsic_block *block,
		int recursion_level)
{
	const struct btrfsic_block_link *l;

	if (recursion_level >= 3 + BTRFS_MAX_LEVEL) {
		/* refer to comment at "abort cyclic linkage (case 1)" */
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("btrfsic: abort cyclic linkage (case 2).\n");

		return 0;
	}

	/*
	 * This algorithm is recursive because the amount of used stack space
	 * is very small and the max recursion depth is limited.
	 */
	list_for_each_entry(l, &block->ref_from_list, node_ref_from) {
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("rl=%d, %c @%llu (%s/%llu/%d) is ref %u* from %c @%llu (%s/%llu/%d)\n",
			       recursion_level,
			       btrfsic_get_block_type(state, block),
			       block->logical_bytenr, block->dev_state->name,
			       block->dev_bytenr, block->mirror_num,
			       l->ref_cnt,
			       btrfsic_get_block_type(state, l->block_ref_from),
			       l->block_ref_from->logical_bytenr,
			       l->block_ref_from->dev_state->name,
			       l->block_ref_from->dev_bytenr,
			       l->block_ref_from->mirror_num);
		if (l->block_ref_from->is_superblock &&
		    state->latest_superblock->dev_bytenr ==
		    l->block_ref_from->dev_bytenr &&
		    state->latest_superblock->dev_state->bdev ==
		    l->block_ref_from->dev_state->bdev)
			return 1;
		else if (btrfsic_is_block_ref_by_superblock(state,
							    l->block_ref_from,
							    recursion_level +
							    1))
			return 1;
	}

	return 0;
}

static void btrfsic_print_add_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l)
{
	pr_info("Add %u* link from %c @%llu (%s/%llu/%d) to %c @%llu (%s/%llu/%d).\n",
	       l->ref_cnt,
	       btrfsic_get_block_type(state, l->block_ref_from),
	       l->block_ref_from->logical_bytenr,
	       l->block_ref_from->dev_state->name,
	       l->block_ref_from->dev_bytenr, l->block_ref_from->mirror_num,
	       btrfsic_get_block_type(state, l->block_ref_to),
	       l->block_ref_to->logical_bytenr,
	       l->block_ref_to->dev_state->name, l->block_ref_to->dev_bytenr,
	       l->block_ref_to->mirror_num);
}

static void btrfsic_print_rem_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l)
{
	pr_info("Rem %u* link from %c @%llu (%s/%llu/%d) to %c @%llu (%s/%llu/%d).\n",
	       l->ref_cnt,
	       btrfsic_get_block_type(state, l->block_ref_from),
	       l->block_ref_from->logical_bytenr,
	       l->block_ref_from->dev_state->name,
	       l->block_ref_from->dev_bytenr, l->block_ref_from->mirror_num,
	       btrfsic_get_block_type(state, l->block_ref_to),
	       l->block_ref_to->logical_bytenr,
	       l->block_ref_to->dev_state->name, l->block_ref_to->dev_bytenr,
	       l->block_ref_to->mirror_num);
}

static char btrfsic_get_block_type(const struct btrfsic_state *state,
				   const struct btrfsic_block *block)
{
	if (block->is_superblock &&
	    state->latest_superblock->dev_bytenr == block->dev_bytenr &&
	    state->latest_superblock->dev_state->bdev == block->dev_state->bdev)
		return 'S';
	else if (block->is_superblock)
		return 's';
	else if (block->is_metadata)
		return 'M';
	else
		return 'D';
}

static void btrfsic_dump_tree(const struct btrfsic_state *state)
{
	btrfsic_dump_tree_sub(state, state->latest_superblock, 0);
}

static void btrfsic_dump_tree_sub(const struct btrfsic_state *state,
				  const struct btrfsic_block *block,
				  int indent_level)
{
	const struct btrfsic_block_link *l;
	int indent_add;
	static char buf[80];
	int cursor_position;

	/*
	 * Should better fill an on-stack buffer with a complete line and
	 * dump it at once when it is time to print a newline character.
	 */

	/*
	 * This algorithm is recursive because the amount of used stack space
	 * is very small and the max recursion depth is limited.
	 */
	indent_add = sprintf(buf, "%c-%llu(%s/%llu/%u)",
			     btrfsic_get_block_type(state, block),
			     block->logical_bytenr, block->dev_state->name,
			     block->dev_bytenr, block->mirror_num);
	if (indent_level + indent_add > BTRFSIC_TREE_DUMP_MAX_INDENT_LEVEL) {
		printk("[...]\n");
		return;
	}
	printk(buf);
	indent_level += indent_add;
	if (list_empty(&block->ref_to_list)) {
		printk("\n");
		return;
	}
	if (block->mirror_num > 1 &&
	    !(state->print_mask & BTRFSIC_PRINT_MASK_TREE_WITH_ALL_MIRRORS)) {
		printk(" [...]\n");
		return;
	}

	cursor_position = indent_level;
	list_for_each_entry(l, &block->ref_to_list, node_ref_to) {
		while (cursor_position < indent_level) {
			printk(" ");
			cursor_position++;
		}
		if (l->ref_cnt > 1)
			indent_add = sprintf(buf, " %d*--> ", l->ref_cnt);
		else
			indent_add = sprintf(buf, " --> ");
		if (indent_level + indent_add >
		    BTRFSIC_TREE_DUMP_MAX_INDENT_LEVEL) {
			printk("[...]\n");
			cursor_position = 0;
			continue;
		}

		printk(buf);

		btrfsic_dump_tree_sub(state, l->block_ref_to,
				      indent_level + indent_add);
		cursor_position = 0;
	}
}

static struct btrfsic_block_link *btrfsic_block_link_lookup_or_add(
		struct btrfsic_state *state,
		struct btrfsic_block_data_ctx *next_block_ctx,
		struct btrfsic_block *next_block,
		struct btrfsic_block *from_block,
		u64 parent_generation)
{
	struct btrfsic_block_link *l;

	l = btrfsic_block_link_hashtable_lookup(next_block_ctx->dev->bdev,
						next_block_ctx->dev_bytenr,
						from_block->dev_state->bdev,
						from_block->dev_bytenr,
						&state->block_link_hashtable);
	if (NULL == l) {
		l = btrfsic_block_link_alloc();
		if (NULL == l) {
			pr_info("btrfsic: error, kmalloc failed!\n");
			return NULL;
		}

		l->block_ref_to = next_block;
		l->block_ref_from = from_block;
		l->ref_cnt = 1;
		l->parent_generation = parent_generation;

		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			btrfsic_print_add_link(state, l);

		list_add(&l->node_ref_to, &from_block->ref_to_list);
		list_add(&l->node_ref_from, &next_block->ref_from_list);

		btrfsic_block_link_hashtable_add(l,
						 &state->block_link_hashtable);
	} else {
		l->ref_cnt++;
		l->parent_generation = parent_generation;
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			btrfsic_print_add_link(state, l);
	}

	return l;
}

static struct btrfsic_block *btrfsic_block_lookup_or_add(
		struct btrfsic_state *state,
		struct btrfsic_block_data_ctx *block_ctx,
		const char *additional_string,
		int is_metadata,
		int is_iodone,
		int never_written,
		int mirror_num,
		int *was_created)
{
	struct btrfsic_block *block;

	block = btrfsic_block_hashtable_lookup(block_ctx->dev->bdev,
					       block_ctx->dev_bytenr,
					       &state->block_hashtable);
	if (NULL == block) {
		struct btrfsic_dev_state *dev_state;

		block = btrfsic_block_alloc();
		if (NULL == block) {
			pr_info("btrfsic: error, kmalloc failed!\n");
			return NULL;
		}
		dev_state = btrfsic_dev_state_lookup(block_ctx->dev->bdev->bd_dev);
		if (NULL == dev_state) {
			pr_info("btrfsic: error, lookup dev_state failed!\n");
			btrfsic_block_free(block);
			return NULL;
		}
		block->dev_state = dev_state;
		block->dev_bytenr = block_ctx->dev_bytenr;
		block->logical_bytenr = block_ctx->start;
		block->is_metadata = is_metadata;
		block->is_iodone = is_iodone;
		block->never_written = never_written;
		block->mirror_num = mirror_num;
		if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
			pr_info("New %s%c-block @%llu (%s/%llu/%d)\n",
			       additional_string,
			       btrfsic_get_block_type(state, block),
			       block->logical_bytenr, dev_state->name,
			       block->dev_bytenr, mirror_num);
		list_add(&block->all_blocks_node, &state->all_blocks_list);
		btrfsic_block_hashtable_add(block, &state->block_hashtable);
		if (NULL != was_created)
			*was_created = 1;
	} else {
		if (NULL != was_created)
			*was_created = 0;
	}

	return block;
}

static void btrfsic_cmp_log_and_dev_bytenr(struct btrfsic_state *state,
					   u64 bytenr,
					   struct btrfsic_dev_state *dev_state,
					   u64 dev_bytenr)
{
	struct btrfs_fs_info *fs_info = state->fs_info;
	struct btrfsic_block_data_ctx block_ctx;
	int num_copies;
	int mirror_num;
	int match = 0;
	int ret;

	num_copies = btrfs_num_copies(fs_info, bytenr, state->metablock_size);

	for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
		ret = btrfsic_map_block(state, bytenr, state->metablock_size,
					&block_ctx, mirror_num);
		if (ret) {
			pr_info("btrfsic: btrfsic_map_block(logical @%llu, mirror %d) failed!\n",
			       bytenr, mirror_num);
			continue;
		}

		if (dev_state->bdev == block_ctx.dev->bdev &&
		    dev_bytenr == block_ctx.dev_bytenr) {
			match++;
			btrfsic_release_block_ctx(&block_ctx);
			break;
		}
		btrfsic_release_block_ctx(&block_ctx);
	}

	if (WARN_ON(!match)) {
		pr_info("btrfs: attempt to write M-block which contains logical bytenr that doesn't map to dev+physical bytenr of submit_bio, buffer->log_bytenr=%llu, submit_bio(bdev=%s, phys_bytenr=%llu)!\n",
		       bytenr, dev_state->name, dev_bytenr);
		for (mirror_num = 1; mirror_num <= num_copies; mirror_num++) {
			ret = btrfsic_map_block(state, bytenr,
						state->metablock_size,
						&block_ctx, mirror_num);
			if (ret)
				continue;

			pr_info("Read logical bytenr @%llu maps to (%s/%llu/%d)\n",
			       bytenr, block_ctx.dev->name,
			       block_ctx.dev_bytenr, mirror_num);
		}
	}
}

static struct btrfsic_dev_state *btrfsic_dev_state_lookup(dev_t dev)
{
	return btrfsic_dev_state_hashtable_lookup(dev,
						  &btrfsic_dev_state_hashtable);
}

int btrfsic_submit_bh(int op, int op_flags, struct buffer_head *bh)
{
	struct btrfsic_dev_state *dev_state;

	if (!btrfsic_is_initialized)
		return submit_bh(op, op_flags, bh);

	mutex_lock(&btrfsic_mutex);
	/* since btrfsic_submit_bh() might also be called before
	 * btrfsic_mount(), this might return NULL */
	dev_state = btrfsic_dev_state_lookup(bh->b_bdev->bd_dev);

	/* Only called to write the superblock (incl. FLUSH/FUA) */
	if (NULL != dev_state &&
	    (op == REQ_OP_WRITE) && bh->b_size > 0) {
		u64 dev_bytenr;

		dev_bytenr = BTRFS_BDEV_BLOCKSIZE * bh->b_blocknr;
		if (dev_state->state->print_mask &
		    BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH)
			pr_info("submit_bh(op=0x%x,0x%x, blocknr=%llu (bytenr %llu), size=%zu, data=%p, bdev=%p)\n",
			       op, op_flags, (unsigned long long)bh->b_blocknr,
			       dev_bytenr, bh->b_size, bh->b_data, bh->b_bdev);
		btrfsic_process_written_block(dev_state, dev_bytenr,
					      &bh->b_data, 1, NULL,
					      NULL, bh, op_flags);
	} else if (NULL != dev_state && (op_flags & REQ_PREFLUSH)) {
		if (dev_state->state->print_mask &
		    BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH)
			pr_info("submit_bh(op=0x%x,0x%x FLUSH, bdev=%p)\n",
			       op, op_flags, bh->b_bdev);
		if (!dev_state->dummy_block_for_bio_bh_flush.is_iodone) {
			if ((dev_state->state->print_mask &
			     (BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH |
			      BTRFSIC_PRINT_MASK_VERBOSE)))
				pr_info("btrfsic_submit_bh(%s) with FLUSH but dummy block already in use (ignored)!\n",
				       dev_state->name);
		} else {
			struct btrfsic_block *const block =
				&dev_state->dummy_block_for_bio_bh_flush;

			block->is_iodone = 0;
			block->never_written = 0;
			block->iodone_w_error = 0;
			block->flush_gen = dev_state->last_flush_gen + 1;
			block->submit_bio_bh_rw = op_flags;
			block->orig_bio_bh_private = bh->b_private;
			block->orig_bio_bh_end_io.bh = bh->b_end_io;
			block->next_in_same_bio = NULL;
			bh->b_private = block;
			bh->b_end_io = btrfsic_bh_end_io;
		}
	}
	mutex_unlock(&btrfsic_mutex);
	return submit_bh(op, op_flags, bh);
}

static void __btrfsic_submit_bio(struct bio *bio)
{
	struct btrfsic_dev_state *dev_state;

	if (!btrfsic_is_initialized)
		return;

	mutex_lock(&btrfsic_mutex);
	/* since btrfsic_submit_bio() is also called before
	 * btrfsic_mount(), this might return NULL */
	dev_state = btrfsic_dev_state_lookup(bio_dev(bio) + bio->bi_partno);
	if (NULL != dev_state &&
	    (bio_op(bio) == REQ_OP_WRITE) && bio_has_data(bio)) {
		unsigned int i = 0;
		u64 dev_bytenr;
		u64 cur_bytenr;
		struct bio_vec bvec;
		struct bvec_iter iter;
		int bio_is_patched;
		char **mapped_datav;
		unsigned int segs = bio_segments(bio);

		dev_bytenr = 512 * bio->bi_iter.bi_sector;
		bio_is_patched = 0;
		if (dev_state->state->print_mask &
		    BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH)
			pr_info("submit_bio(rw=%d,0x%x, bi_vcnt=%u, bi_sector=%llu (bytenr %llu), bi_disk=%p)\n",
			       bio_op(bio), bio->bi_opf, segs,
			       (unsigned long long)bio->bi_iter.bi_sector,
			       dev_bytenr, bio->bi_disk);

		mapped_datav = kmalloc_array(segs,
					     sizeof(*mapped_datav), GFP_NOFS);
		if (!mapped_datav)
			goto leave;
		cur_bytenr = dev_bytenr;

		bio_for_each_segment(bvec, bio, iter) {
			BUG_ON(bvec.bv_len != PAGE_SIZE);
			mapped_datav[i] = kmap(bvec.bv_page);
			i++;

			if (dev_state->state->print_mask &
			    BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH_VERBOSE)
				pr_info("#%u: bytenr=%llu, len=%u, offset=%u\n",
				       i, cur_bytenr, bvec.bv_len, bvec.bv_offset);
			cur_bytenr += bvec.bv_len;
		}
		btrfsic_process_written_block(dev_state, dev_bytenr,
					      mapped_datav, segs,
					      bio, &bio_is_patched,
					      NULL, bio->bi_opf);
		bio_for_each_segment(bvec, bio, iter)
			kunmap(bvec.bv_page);
		kfree(mapped_datav);
	} else if (NULL != dev_state && (bio->bi_opf & REQ_PREFLUSH)) {
		if (dev_state->state->print_mask &
		    BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH)
			pr_info("submit_bio(rw=%d,0x%x FLUSH, disk=%p)\n",
			       bio_op(bio), bio->bi_opf, bio->bi_disk);
		if (!dev_state->dummy_block_for_bio_bh_flush.is_iodone) {
			if ((dev_state->state->print_mask &
			     (BTRFSIC_PRINT_MASK_SUBMIT_BIO_BH |
			      BTRFSIC_PRINT_MASK_VERBOSE)))
				pr_info("btrfsic_submit_bio(%s) with FLUSH but dummy block already in use (ignored)!\n",
				       dev_state->name);
		} else {
			struct btrfsic_block *const block =
				&dev_state->dummy_block_for_bio_bh_flush;

			block->is_iodone = 0;
			block->never_written = 0;
			block->iodone_w_error = 0;
			block->flush_gen = dev_state->last_flush_gen + 1;
			block->submit_bio_bh_rw = bio->bi_opf;
			block->orig_bio_bh_private = bio->bi_private;
			block->orig_bio_bh_end_io.bio = bio->bi_end_io;
			block->next_in_same_bio = NULL;
			bio->bi_private = block;
			bio->bi_end_io = btrfsic_bio_end_io;
		}
	}
leave:
	mutex_unlock(&btrfsic_mutex);
}

void btrfsic_submit_bio(struct bio *bio)
{
	__btrfsic_submit_bio(bio);
	submit_bio(bio);
}

int btrfsic_submit_bio_wait(struct bio *bio)
{
	__btrfsic_submit_bio(bio);
	return submit_bio_wait(bio);
}

int btrfsic_mount(struct btrfs_fs_info *fs_info,
		  struct btrfs_fs_devices *fs_devices,
		  int including_extent_data, u32 print_mask)
{
	int ret;
	struct btrfsic_state *state;
	struct list_head *dev_head = &fs_devices->devices;
	struct btrfs_device *device;

	if (fs_info->nodesize & ((u64)PAGE_SIZE - 1)) {
		pr_info("btrfsic: cannot handle nodesize %d not being a multiple of PAGE_SIZE %ld!\n",
		       fs_info->nodesize, PAGE_SIZE);
		return -1;
	}
	if (fs_info->sectorsize & ((u64)PAGE_SIZE - 1)) {
		pr_info("btrfsic: cannot handle sectorsize %d not being a multiple of PAGE_SIZE %ld!\n",
		       fs_info->sectorsize, PAGE_SIZE);
		return -1;
	}
	state = kvzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		pr_info("btrfs check-integrity: allocation failed!\n");
		return -ENOMEM;
	}

	if (!btrfsic_is_initialized) {
		mutex_init(&btrfsic_mutex);
		btrfsic_dev_state_hashtable_init(&btrfsic_dev_state_hashtable);
		btrfsic_is_initialized = 1;
	}
	mutex_lock(&btrfsic_mutex);
	state->fs_info = fs_info;
	state->print_mask = print_mask;
	state->include_extent_data = including_extent_data;
	state->csum_size = 0;
	state->metablock_size = fs_info->nodesize;
	state->datablock_size = fs_info->sectorsize;
	INIT_LIST_HEAD(&state->all_blocks_list);
	btrfsic_block_hashtable_init(&state->block_hashtable);
	btrfsic_block_link_hashtable_init(&state->block_link_hashtable);
	state->max_superblock_generation = 0;
	state->latest_superblock = NULL;

	list_for_each_entry(device, dev_head, dev_list) {
		struct btrfsic_dev_state *ds;
		const char *p;

		if (!device->bdev || !device->name)
			continue;

		ds = btrfsic_dev_state_alloc();
		if (NULL == ds) {
			pr_info("btrfs check-integrity: kmalloc() failed!\n");
			mutex_unlock(&btrfsic_mutex);
			return -ENOMEM;
		}
		ds->bdev = device->bdev;
		ds->state = state;
		bdevname(ds->bdev, ds->name);
		ds->name[BDEVNAME_SIZE - 1] = '\0';
		p = kbasename(ds->name);
		strlcpy(ds->name, p, sizeof(ds->name));
		btrfsic_dev_state_hashtable_add(ds,
						&btrfsic_dev_state_hashtable);
	}

	ret = btrfsic_process_superblock(state, fs_devices);
	if (0 != ret) {
		mutex_unlock(&btrfsic_mutex);
		btrfsic_unmount(fs_devices);
		return ret;
	}

	if (state->print_mask & BTRFSIC_PRINT_MASK_INITIAL_DATABASE)
		btrfsic_dump_database(state);
	if (state->print_mask & BTRFSIC_PRINT_MASK_INITIAL_TREE)
		btrfsic_dump_tree(state);

	mutex_unlock(&btrfsic_mutex);
	return 0;
}

void btrfsic_unmount(struct btrfs_fs_devices *fs_devices)
{
	struct btrfsic_block *b_all, *tmp_all;
	struct btrfsic_state *state;
	struct list_head *dev_head = &fs_devices->devices;
	struct btrfs_device *device;

	if (!btrfsic_is_initialized)
		return;

	mutex_lock(&btrfsic_mutex);

	state = NULL;
	list_for_each_entry(device, dev_head, dev_list) {
		struct btrfsic_dev_state *ds;

		if (!device->bdev || !device->name)
			continue;

		ds = btrfsic_dev_state_hashtable_lookup(
				device->bdev->bd_dev,
				&btrfsic_dev_state_hashtable);
		if (NULL != ds) {
			state = ds->state;
			btrfsic_dev_state_hashtable_remove(ds);
			btrfsic_dev_state_free(ds);
		}
	}

	if (NULL == state) {
		pr_info("btrfsic: error, cannot find state information on umount!\n");
		mutex_unlock(&btrfsic_mutex);
		return;
	}

	/*
	 * Don't care about keeping the lists' state up to date,
	 * just free all memory that was allocated dynamically.
	 * Free the blocks and the block_links.
	 */
	list_for_each_entry_safe(b_all, tmp_all, &state->all_blocks_list,
				 all_blocks_node) {
		struct btrfsic_block_link *l, *tmp;

		list_for_each_entry_safe(l, tmp, &b_all->ref_to_list,
					 node_ref_to) {
			if (state->print_mask & BTRFSIC_PRINT_MASK_VERBOSE)
				btrfsic_print_rem_link(state, l);

			l->ref_cnt--;
			if (0 == l->ref_cnt)
				btrfsic_block_link_free(l);
		}

		if (b_all->is_iodone || b_all->never_written)
			btrfsic_block_free(b_all);
		else
			pr_info("btrfs: attempt to free %c-block @%llu (%s/%llu/%d) on umount which is not yet iodone!\n",
			       btrfsic_get_block_type(state, b_all),
			       b_all->logical_bytenr, b_all->dev_state->name,
			       b_all->dev_bytenr, b_all->mirror_num);
	}

	mutex_unlock(&btrfsic_mutex);

	kvfree(state);
}
