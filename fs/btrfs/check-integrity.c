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
#include <linux/mutex.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <crypto/hash.h>
#include "messages.h"
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
#include "accessors.h"

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
	void *orig_bio_private;
	bio_end_io_t *orig_bio_end_io;
	blk_opf_t submit_bio_bh_rw;
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
	struct list_head all_blocks_list;
	struct btrfsic_block_hashtable block_hashtable;
	struct btrfsic_block_link_hashtable block_link_hashtable;
	struct btrfs_fs_info *fs_info;
	u64 max_superblock_generation;
	struct btrfsic_block *latest_superblock;
	u32 metablock_size;
	u32 datablock_size;
};

static void btrfsic_print_rem_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l);
static char btrfsic_get_block_type(const struct btrfsic_state *state,
				   const struct btrfsic_block *block);
static struct mutex btrfsic_mutex;
static int btrfsic_is_initialized;
static struct btrfsic_dev_state_hashtable btrfsic_dev_state_hashtable;


static void btrfsic_block_free(struct btrfsic_block *b)
{
	BUG_ON(!(NULL == b || BTRFSIC_BLOCK_MAGIC_NUMBER == b->magic_num));
	kfree(b);
}

static void btrfsic_block_link_free(struct btrfsic_block_link *l)
{
	BUG_ON(!(NULL == l || BTRFSIC_BLOCK_LINK_MAGIC_NUMBER == l->magic_num));
	kfree(l);
}

static void btrfsic_dev_state_free(struct btrfsic_dev_state *ds)
{
	BUG_ON(!(NULL == ds ||
		 BTRFSIC_DEV2STATE_MAGIC_NUMBER == ds->magic_num));
	kfree(ds);
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

static void btrfsic_print_rem_link(const struct btrfsic_state *state,
				   const struct btrfsic_block_link *l)
{
	pr_info("rem %u* link from %c @%llu (%pg/%llu/%d) to %c @%llu (%pg/%llu/%d)\n",
	       l->ref_cnt,
	       btrfsic_get_block_type(state, l->block_ref_from),
	       l->block_ref_from->logical_bytenr,
	       l->block_ref_from->dev_state->bdev,
	       l->block_ref_from->dev_bytenr, l->block_ref_from->mirror_num,
	       btrfsic_get_block_type(state, l->block_ref_to),
	       l->block_ref_to->logical_bytenr,
	       l->block_ref_to->dev_state->bdev, l->block_ref_to->dev_bytenr,
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
			pr_info(
"btrfs: attempt to free %c-block @%llu (%pg/%llu/%d) on umount which is not yet iodone!\n",
			       btrfsic_get_block_type(state, b_all),
			       b_all->logical_bytenr, b_all->dev_state->bdev,
			       b_all->dev_bytenr, b_all->mirror_num);
	}

	mutex_unlock(&btrfsic_mutex);

	kvfree(state);
}
