// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/inode.c
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iomap.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/time64.h>
#include <linux/writeback.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "xattr.h"
#include "truncate.h"

/*
 * For constructing the nonnegative timestamp lower bound value.
 * binary: 00000000 00000000 00000000 00000000
 */
#define LOWER_MSB_0 0L
/*
 * For constructing the nonnegative timestamp upper bound value.
 * binary: 01111111 11111111 11111111 11111111
 *
 */
#define UPPER_MSB_0 0x7fffffffL
/*
 * For constructing the negative timestamp lower bound value.
 * binary: 10000000 00000000 00000000 00000000
 */
#define LOWER_MSB_1 (-(UPPER_MSB_0) - 1L)  /* avoid overflow */
/*
 * For constructing the negative timestamp upper bound value.
 * binary: 11111111 11111111 11111111 11111111
 */
#define UPPER_MSB_1 (-1L)
/*
 * Upper bound for nanoseconds value supported by the encoding.
 * binary: 00111111 11111111 11111111 11111111
 */
#define MAX_NANOSECONDS ((1L << 30) - 1)

#define CASE_NAME_FORMAT "%s: msb:%x lower_bound:%x extra_bits: %x"

#define LOWER_BOUND_NEG_NO_EXTRA_BITS_CASE\
	"1901-12-13 Lower bound of 32bit < 0 timestamp, no extra bits"
#define UPPER_BOUND_NEG_NO_EXTRA_BITS_CASE\
	"1969-12-31 Upper bound of 32bit < 0 timestamp, no extra bits"
#define LOWER_BOUND_NONNEG_NO_EXTRA_BITS_CASE\
	"1970-01-01 Lower bound of 32bit >=0 timestamp, no extra bits"
#define UPPER_BOUND_NONNEG_NO_EXTRA_BITS_CASE\
	"2038-01-19 Upper bound of 32bit >=0 timestamp, no extra bits"
#define LOWER_BOUND_NEG_LO_1_CASE\
	"2038-01-19 Lower bound of 32bit <0 timestamp, lo extra sec bit on"
#define UPPER_BOUND_NEG_LO_1_CASE\
	"2106-02-07 Upper bound of 32bit <0 timestamp, lo extra sec bit on"
#define LOWER_BOUND_NONNEG_LO_1_CASE\
	"2106-02-07 Lower bound of 32bit >=0 timestamp, lo extra sec bit on"
#define UPPER_BOUND_NONNEG_LO_1_CASE\
	"2174-02-25 Upper bound of 32bit >=0 timestamp, lo extra sec bit on"
#define LOWER_BOUND_NEG_HI_1_CASE\
	"2174-02-25 Lower bound of 32bit <0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NEG_HI_1_CASE\
	"2242-03-16 Upper bound of 32bit <0 timestamp, hi extra sec bit on"
#define LOWER_BOUND_NONNEG_HI_1_CASE\
	"2242-03-16 Lower bound of 32bit >=0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NONNEG_HI_1_CASE\
	"2310-04-04 Upper bound of 32bit >=0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NONNEG_HI_1_NS_1_CASE\
	"2310-04-04 Upper bound of 32bit>=0 timestamp, hi extra sec bit 1. 1 ns"
#define LOWER_BOUND_NONNEG_HI_1_NS_MAX_CASE\
	"2378-04-22 Lower bound of 32bit>= timestamp. Extra sec bits 1. Max ns"
#define LOWER_BOUND_NONNEG_EXTRA_BITS_1_CASE\
	"2378-04-22 Lower bound of 32bit >=0 timestamp. All extra sec bits on"
#define UPPER_BOUND_NONNEG_EXTRA_BITS_1_CASE\
	"2446-05-10 Upper bound of 32bit >=0 timestamp. All extra sec bits on"

struct timestamp_expectation {
	const char *test_case_name;
	struct timespec64 expected;
	u32 extra_bits;
	bool msb_set;
	bool lower_bound;
};

static const struct timestamp_expectation test_data[] = {
	{
		.test_case_name = LOWER_BOUND_NEG_NO_EXTRA_BITS_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits = 0,
		.expected = {.tv_sec = -0x80000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_NO_EXTRA_BITS_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 0,
		.expected = {.tv_sec = -1LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_NO_EXTRA_BITS_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 0,
		.expected = {0LL, 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_NO_EXTRA_BITS_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 0,
		.expected = {.tv_sec = 0x7fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NEG_LO_1_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x80000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_LO_1_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 1,
		.expected = {.tv_sec = 0xffffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_LO_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x100000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_LO_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x17fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NEG_HI_1_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits =  2,
		.expected = {.tv_sec = 0x180000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_HI_1_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x1ffffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_HI_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x200000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_HI_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x27fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_HI_1_NS_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 6,
		.expected = {.tv_sec = 0x27fffffffLL, .tv_nsec = 1L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_HI_1_NS_MAX_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 0xFFFFFFFF,
		.expected = {.tv_sec = 0x300000000LL,
			     .tv_nsec = MAX_NANOSECONDS},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_EXTRA_BITS_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 3,
		.expected = {.tv_sec = 0x300000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_EXTRA_BITS_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 3,
		.expected = {.tv_sec = 0x37fffffffLL, .tv_nsec = 0L},
	}
};

static void timestamp_expectation_to_desc(const struct timestamp_expectation *t,
					  char *desc)
{
	strscpy(desc, t->test_case_name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ext4_inode, test_data, timestamp_expectation_to_desc);

static time64_t get_32bit_time(const struct timestamp_expectation * const test)
{
	if (test->msb_set) {
		if (test->lower_bound)
			return LOWER_MSB_1;

		return UPPER_MSB_1;
	}

	if (test->lower_bound)
		return LOWER_MSB_0;
	return UPPER_MSB_0;
}

/*
 *  Test data is derived from the table in the Inode Timestamps section of
 *  Documentation/filesystems/ext4/inodes.rst.
 */
static void inode_test_xtimestamp_decoding(struct kunit *test)
{
	struct timespec64 timestamp;

	struct timestamp_expectation *test_param =
			(struct timestamp_expectation *)(test->param_value);

	timestamp = ext4_decode_extra_time(
				cpu_to_le32(get_32bit_time(test_param)),
				cpu_to_le32(test_param->extra_bits));

	KUNIT_EXPECT_EQ_MSG(test,
			    test_param->expected.tv_sec,
			    timestamp.tv_sec,
			    CASE_NAME_FORMAT,
			    test_param->test_case_name,
			    test_param->msb_set,
			    test_param->lower_bound,
			    test_param->extra_bits);
	KUNIT_EXPECT_EQ_MSG(test,
			    test_param->expected.tv_nsec,
			    timestamp.tv_nsec,
			    CASE_NAME_FORMAT,
			    test_param->test_case_name,
			    test_param->msb_set,
			    test_param->lower_bound,
			    test_param->extra_bits);
}

/* -------------------------------------------------------------------------
 * Mock filesystem, block pool, and VFS inode fixture for fs/ext4/inode.c
 * -------------------------------------------------------------------------
 */

#define INODE_TEST_BLK_SIZE		4096
#define INODE_TEST_BLK_BITS		12
#define INODE_TEST_TOTAL_BLKS		32768
#define INODE_TEST_INODE_SIZE		256
#define INODE_TEST_EXTRA_ISIZE		32
#define INODE_TEST_INODES_PER_BLK	(INODE_TEST_BLK_SIZE / INODE_TEST_INODE_SIZE)
#define INODE_TEST_INODES_PER_GRP	2048
#define INODE_TEST_FIRST_INO		11
#define INODE_TEST_PRIMARY_INO		12
#define INODE_TEST_CSUM_SEED		0x5a3c9e71U

#define INODE_TEST_IBITMAP_BLK		10
#define INODE_TEST_ITABLE_BLK0		20
#define INODE_TEST_ITABLE_BLK1		21
#define INODE_TEST_META_BLK0		50
#define INODE_TEST_META_BLK1		51
#define INODE_TEST_DATA_BLK0		100
#define INODE_TEST_DATA_NBLKS		8
#define INODE_TEST_POOL_SIZE		16
#define INODE_TEST_MAX_TRACKED_INODES	32

struct inode_test_blk {
	bool in_use;
	bool in_cache;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	char cache_buf[INODE_TEST_BLK_SIZE] __aligned(8);
	char disk_buf[INODE_TEST_BLK_SIZE] __aligned(8);
};

struct inode_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct ext4_group_desc gdp;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct buffer_head sb_bh;
	struct dentry root_dentry;

	struct ext4_inode_info ei;
	struct ext4_inode_info scratch_ei;
	union {
		struct ext4_inode raw;
		u8 bytes[INODE_TEST_INODE_SIZE];
	} scratch_raw;
	struct inode *tracked_inodes[INODE_TEST_MAX_TRACKED_INODES];
	unsigned int nr_tracked_inodes;

	struct inode_test_blk pool[INODE_TEST_POOL_SIZE];

	bool fail_group_desc;
	bool fail_getblk;
	bool fail_read_eio;
	int fail_zeroout_err;
	int fail_alloc_err;
	int fail_orphan_err;

	ext4_fsblk_t next_alloc_pblk;
	unsigned int alloc_calls;
	unsigned int free_calls;
	ext4_fsblk_t last_free_pblk;
	unsigned long last_free_count;
	unsigned long total_freed_blocks;
	unsigned int zeroout_calls;
	ext4_lblk_t last_zeroout_lblk;
	ext4_fsblk_t last_zeroout_pblk;
	ext4_lblk_t last_zeroout_len;
	unsigned int read_nowait_calls;
	unsigned int read_lock_calls;
	unsigned int readahead_calls;
	ext4_fsblk_t first_readahead_blk;
	ext4_fsblk_t last_readahead_blk;
	unsigned int orphan_add_calls;
	unsigned int orphan_del_calls;
	unsigned int free_inode_calls;
};

static unsigned long inode_test_shrinker_count(struct shrinker *shrink,
					       struct shrink_control *sc)
{
	return 0;
}

static unsigned long inode_test_shrinker_scan(struct shrinker *shrink,
					      struct shrink_control *sc)
{
	return SHRINK_STOP;
}

static void inode_test_init_ei_fields(struct ext4_inode_info *ei)
{
	inode_set_iversion(&ei->vfs_inode, 1);
	ei->i_flags = 0;
	ext4_clear_state_flags(ei);
	spin_lock_init(&ei->i_raw_lock);
	ei->i_prealloc_node = RB_ROOT;
	atomic_set(&ei->i_prealloc_active, 0);
	rwlock_init(&ei->i_prealloc_lock);
	ext4_es_init_tree(&ei->i_es_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_list);
	ei->i_es_all_nr = 0;
	ei->i_es_shk_nr = 0;
	ei->i_es_shrink_lblk = 0;
	ei->i_es_seq = 0;
	ei->i_reserved_data_blocks = 0;
	spin_lock_init(&ei->i_block_reservation_lock);
	ext4_init_pending_tree(&ei->i_pending_tree);
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
	memset(&ei->i_dquot, 0, sizeof(ei->i_dquot));
#endif
	ei->jinode = NULL;
	init_rwsem(&ei->xattr_sem);
	INIT_LIST_HEAD(&ei->i_rsv_conversion_list);
	spin_lock_init(&ei->i_completed_io_lock);
	ei->i_sync_tid = 0;
	ei->i_datasync_tid = 0;
	INIT_WORK(&ei->i_rsv_conversion_work, ext4_end_io_rsv_work);
	INIT_LIST_HEAD(&ei->i_orphan);
	ext4_fc_init_inode(&ei->vfs_inode);
	spin_lock_init(&ei->i_fc_lock);
	ei->i_metadata_bhs = NULL;
	ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	ei->i_extra_isize = INODE_TEST_EXTRA_ISIZE;
	ei->i_csum_seed = INODE_TEST_CSUM_SEED;
}

static void inode_test_cleanup_ext4_trees(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct rb_node *node;

	down_write(&ei->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	while ((node = rb_first(&ei->i_pending_tree.root)) != NULL) {
		struct pending_reservation *pr;

		pr = rb_entry(node, struct pending_reservation, rb_node);
		ext4_remove_pending(inode, EXT4_C2B(sbi, pr->lclu));
	}
	up_write(&ei->i_data_sem);
}

static struct inode *inode_test_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	inode_test_init_ei_fields(ei);
	return &ei->vfs_inode;
}

static void inode_test_destroy_inode(struct inode *inode)
{
	inode_test_cleanup_ext4_trees(inode);
	kfree(ext4_i_metadata_bhs(inode));
	EXT4_I(inode)->i_metadata_bhs = NULL;
}

static void inode_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **inode_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *inode_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations inode_test_qops = {
	.get_reserved_space = inode_test_get_reserved_space,
};
#endif

static const struct super_operations inode_test_sops = {
	.alloc_inode	= inode_test_alloc_inode,
	.evict_inode	= ext4_evict_inode,
	.destroy_inode	= inode_test_destroy_inode,
	.free_inode	= inode_test_free_inode,
	.drop_inode	= inode_just_drop,
#ifdef CONFIG_QUOTA
	.get_dquots	= inode_test_get_dquots,
#endif
};

static int inode_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type inode_test_fs_type = {
	.name		 = "ext4_inode_kunit",
	.init_fs_context = inode_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static struct inode_test_blk *inode_test_find_blk(struct inode_test_ctx *ctx,
						  ext4_fsblk_t blocknr)
{
	int i;

	for (i = 0; i < INODE_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && ctx->pool[i].blocknr == blocknr)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct inode_test_blk *inode_test_find_blk_by_bh(
		struct inode_test_ctx *ctx, struct buffer_head *bh)
{
	int i;

	for (i = 0; i < INODE_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && &ctx->pool[i].bh == bh)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct inode_test_blk *inode_test_setup_blk(struct inode_test_ctx *ctx,
						   int idx,
						   ext4_fsblk_t blocknr,
						   bool in_cache,
						   bool uptodate)
{
	struct inode_test_blk *slot = &ctx->pool[idx];

	memset(slot, 0, sizeof(*slot));
	slot->in_use = true;
	slot->in_cache = in_cache;
	slot->blocknr = blocknr;
	slot->bh.b_bdev = &ctx->bdev;
	slot->bh.b_blocknr = blocknr;
	slot->bh.b_size = INODE_TEST_BLK_SIZE;
	slot->bh.b_data = slot->cache_buf;
	atomic_set(&slot->bh.b_count, 1);
	if (uptodate)
		set_buffer_uptodate(&slot->bh);
	return slot;
}

/* --- Static Stubs --- */

static struct ext4_group_desc *inode_test_get_group_desc_stub(
		struct super_block *sb, ext4_group_t block_group,
		struct buffer_head **bh)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx || ctx->fail_group_desc || block_group != 0)
		return NULL;
	if (bh)
		*bh = NULL;
	return &ctx->gdp;
}

static struct buffer_head *inode_test_sb_getblk_stub(struct super_block *sb,
						     ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;
	struct inode_test_blk *slot;

	if (!ctx || ctx->fail_getblk)
		return NULL;
	slot = inode_test_find_blk(ctx, block);
	if (!slot)
		return NULL;
	slot->in_cache = true;
	get_bh(&slot->bh);
	return &slot->bh;
}

static struct buffer_head *inode_test_sb_find_get_block_stub(
		struct super_block *sb, ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;
	struct inode_test_blk *slot;

	if (!ctx)
		return NULL;
	slot = inode_test_find_blk(ctx, block);
	if (!slot || !slot->in_cache)
		return NULL;
	get_bh(&slot->bh);
	return &slot->bh;
}

static struct buffer_head *inode_test_sb_getblk_unmovable_stub(
		struct super_block *sb, ext4_fsblk_t block)
{
	return inode_test_sb_getblk_stub(sb, block);
}

static struct buffer_head *inode_test_ext_getblk_stub(struct inode *inode,
						      ext4_fsblk_t pblk,
						      gfp_t gfp_flags)
{
	struct buffer_head *bh;

	bh = inode_test_sb_getblk_stub(inode->i_sb, pblk);
	if (bh) {
		set_buffer_uptodate(bh);
		set_buffer_verified(bh);
	}
	return bh;
}

static void inode_test_read_bh_nowait_stub(struct buffer_head *bh,
					   blk_opf_t op_flags,
					   bio_end_io_t end_io,
					   bool simu_fail)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;
	struct inode_test_blk *slot;

	if (!ctx) {
		unlock_buffer(bh);
		return;
	}
	ctx->read_nowait_calls++;
	if (ctx->fail_read_eio || simu_fail) {
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		return;
	}
	slot = inode_test_find_blk_by_bh(ctx, bh);
	if (!slot)
		slot = inode_test_find_blk(ctx, bh->b_blocknr);
	if (slot) {
		if (bh->b_folio)
			memcpy_to_folio(bh->b_folio, bh_offset(bh),
					slot->disk_buf, bh->b_size);
		else
			memcpy(bh->b_data, slot->disk_buf, bh->b_size);
		set_buffer_uptodate(bh);
	} else {
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
}

static int inode_test_read_bh_lock_stub(struct buffer_head *bh,
					blk_opf_t op_flags, bool wait)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;
	struct inode_test_blk *slot;

	if (!ctx)
		return -EIO;
	ctx->read_lock_calls++;
	lock_buffer(bh);
	if (ctx->fail_read_eio) {
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		return wait ? -EIO : 0;
	}
	slot = inode_test_find_blk_by_bh(ctx, bh);
	if (!slot)
		slot = inode_test_find_blk(ctx, bh->b_blocknr);
	if (!slot) {
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		return wait ? -EIO : 0;
	}
	if (bh->b_folio)
		memcpy_to_folio(bh->b_folio, bh_offset(bh), slot->disk_buf,
				bh->b_size);
	else
		memcpy(bh->b_data, slot->disk_buf, bh->b_size);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	return 0;
}

static void inode_test_breadahead_stub(struct super_block *sb, sector_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return;
	if (ctx->readahead_calls == 0)
		ctx->first_readahead_blk = block;
	ctx->last_readahead_blk = block;
	ctx->readahead_calls++;
}

static ext4_fsblk_t inode_test_mb_new_blocks_stub(
		handle_t *handle, struct ext4_allocation_request *ar, int *errp)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;
	ext4_fsblk_t pblk;

	if (!ctx) {
		*errp = -EIO;
		return 0;
	}
	if (ctx->fail_alloc_err) {
		*errp = ctx->fail_alloc_err;
		return 0;
	}
	ctx->alloc_calls++;
	pblk = ctx->next_alloc_pblk;
	ctx->next_alloc_pblk += ar->len;
	*errp = 0;
	return pblk;
}

static void inode_test_free_blocks_stub(handle_t *handle, struct inode *inode,
					struct buffer_head *bh,
					ext4_fsblk_t block,
					unsigned long count, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (bh && (flags & EXT4_FREE_BLOCKS_FORGET))
		brelse(bh);
	if (!ctx)
		return;
	ctx->free_calls++;
	ctx->last_free_pblk = block;
	ctx->last_free_count = count;
	ctx->total_freed_blocks += count;
}

static int inode_test_issue_zeroout_stub(handle_t *handle, struct inode *inode,
					 ext4_lblk_t lblk, ext4_fsblk_t pblk,
					 ext4_lblk_t len)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;
	ctx->zeroout_calls++;
	ctx->last_zeroout_lblk = lblk;
	ctx->last_zeroout_pblk = pblk;
	ctx->last_zeroout_len = len;
	return ctx->fail_zeroout_err;
}

static int inode_test_dirty_metadata_stub(const char *where, unsigned int line,
					  handle_t *handle,
					  struct inode *inode,
					  struct buffer_head *bh)
{
	if (bh) {
		set_buffer_uptodate(bh);
		set_buffer_dirty(bh);
	}
	return 0;
}

static int inode_test_ext_dirty_stub(const char *where, unsigned int line,
				     handle_t *handle, struct inode *inode,
				     struct ext4_ext_path *path)
{
	if (path && path->p_bh) {
		ext4_extent_block_csum_set_test(inode,
						ext_block_hdr(path->p_bh));
		set_buffer_verified(path->p_bh);
	}
	return 0;
}

static int inode_test_orphan_add_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;
	if (ctx->fail_orphan_err)
		return ctx->fail_orphan_err;
	ctx->orphan_add_calls++;
	return 0;
}

static int inode_test_orphan_del_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;
	ctx->orphan_del_calls++;
	return 0;
}

static void inode_test_free_inode_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct inode_test_ctx *ctx = test ? test->priv : NULL;

	if (ctx)
		ctx->free_inode_calls++;
	ext4_clear_inode(inode);
}

/* --- Fixture Setup & Cleanup --- */

static void inode_test_track_iget_inode(struct kunit *test,
					struct inode_test_ctx *ctx,
					struct inode *inode)
{
	KUNIT_ASSERT_LT(test, ctx->nr_tracked_inodes,
			(unsigned int)INODE_TEST_MAX_TRACKED_INODES);
	ctx->tracked_inodes[ctx->nr_tracked_inodes++] = inode;
}

static void inode_test_put_tracked_inode(struct inode_test_ctx *ctx,
					 struct inode *inode)
{
	unsigned int i;

	for (i = 0; i < ctx->nr_tracked_inodes; i++) {
		if (ctx->tracked_inodes[i] == inode) {
			ctx->tracked_inodes[i] =
				ctx->tracked_inodes[--ctx->nr_tracked_inodes];
			ctx->tracked_inodes[ctx->nr_tracked_inodes] = NULL;
			iput(inode);
			return;
		}
	}
	iput(inode);
}

static void inode_test_reset_mock_inode(struct inode_test_ctx *ctx, umode_t mode)
{
	struct ext4_inode_info *ei = &ctx->ei;
	struct inode *inode = &ei->vfs_inode;
	struct mapping_metadata_bhs *mmb = ext4_i_metadata_bhs(inode);

	truncate_inode_pages_final(&inode->i_data);
	inode_io_list_del(inode);
	ext4_fc_del(inode);
	if (mmb)
		mmb_invalidate(mmb);
	inode_test_cleanup_ext4_trees(inode);
	if (ei->jinode) {
		jbd2_free_inode(ei->jinode);
		ei->jinode = NULL;
	}

	inode_state_assign_raw(inode, 0);
	inode->i_ino = INODE_TEST_PRIMARY_INO;
	inode->i_mode = mode;
	inode->i_blkbits = ctx->sb->s_blocksize_bits;
	inode->i_flags = S_NOQUOTA;
	set_nlink(inode, 1);
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->i_generation = 1;
	inode->i_rdev = 0;
	ei->i_disksize = 0;
	ei->i_flags = 0;
	ei->i_file_acl = 0;
	ei->i_dtime = 0;
	ei->i_inline_off = 0;
	ei->i_inline_size = 0;
	ei->i_extra_isize = INODE_TEST_EXTRA_ISIZE;
	ei->i_reserved_data_blocks = 0;
	ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	ext4_clear_state_flags(ei);
	memset(ei->i_data, 0, sizeof(ei->i_data));

	if (S_ISREG(mode) || S_ISDIR(mode)) {
		ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
		ext4_ext_tree_init(NULL, inode);
	}
	if (S_ISREG(mode))
		ext4_set_aops(inode);
}

static void inode_test_cleanup(void *data)
{
	struct inode_test_ctx *ctx = data;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct mapping_metadata_bhs *mmb;
	unsigned int i;

	ctx->sbi.s_journal = NULL;

	for (i = 0; i < ctx->nr_tracked_inodes; i++) {
		struct inode *ti = ctx->tracked_inodes[i];

		if (ti) {
			spin_lock(&ti->i_lock);
			inode_state_clear(ti, I_DIRTY_TIME);
			spin_unlock(&ti->i_lock);
			iput(ti);
			ctx->tracked_inodes[i] = NULL;
		}
	}
	ctx->nr_tracked_inodes = 0;

	truncate_inode_pages_final(&inode->i_data);
	inode_io_list_del(inode);
	ext4_fc_del(inode);
	mmb = ext4_i_metadata_bhs(inode);
	if (mmb)
		mmb_invalidate(mmb);
	inode_test_cleanup_ext4_trees(inode);
	if (ctx->ei.jinode) {
		jbd2_free_inode(ctx->ei.jinode);
		ctx->ei.jinode = NULL;
	}

	deactivate_super(ctx->sb);
	rcu_barrier();

	ext4_es_unregister_shrinker(&ctx->sbi);
	ext4_es_destroy_stats(&ctx->sbi);
	percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
	percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);

	kfree(mmb);
	kfree(ctx);
}

static struct inode_test_ctx *inode_test_setup(struct kunit *test)
{
	struct inode_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *inode, *root_inode;
	int err, i;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	fc = fs_context_for_mount(&inode_test_fs_type, 0);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	sb = sget_fc(fc, NULL, set_anon_super_fc);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	ctx->sb = sb;
	sbi = &ctx->sbi;

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &inode_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &inode_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_blocksize = INODE_TEST_BLK_SIZE;
	sb->s_blocksize_bits = INODE_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_inode_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = INODE_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(1);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	ctx->es.s_inodes_count = cpu_to_le32(INODE_TEST_INODES_PER_GRP);
	ctx->es.s_inodes_per_group = cpu_to_le32(INODE_TEST_INODES_PER_GRP);
	ctx->es.s_first_ino = cpu_to_le32(INODE_TEST_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(INODE_TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(INODE_TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(INODE_TEST_EXTRA_ISIZE);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_blocks_count_set(&ctx->es, INODE_TEST_TOTAL_BLKS);
	ext4_set_feature_extents(sb);
	ext4_set_feature_extra_isize(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);
	ext4_set_feature_huge_file(sb);
	ext4_set_feature_large_file(sb);
	ext4_set_feature_dir_index(sb);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	sbi->s_inode_size = INODE_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = INODE_TEST_INODES_PER_BLK;
	sbi->s_inodes_per_group = INODE_TEST_INODES_PER_GRP;
	sbi->s_first_ino = INODE_TEST_FIRST_INO;
	sbi->s_want_extra_isize = INODE_TEST_EXTRA_ISIZE;
	sbi->s_blocks_per_group = INODE_TEST_TOTAL_BLKS;
	sbi->s_clusters_per_group = INODE_TEST_TOTAL_BLKS;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = 1;
	sbi->s_gdb_count = 1;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = INODE_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_bitmap_maxbytes = 0x1ff7ffff000ULL;
	sbi->s_extent_max_zeroout_kb = 32;
	sbi->s_csum_seed = INODE_TEST_CSUM_SEED;

	ext4_inode_bitmap_set(sb, &ctx->gdp, INODE_TEST_IBITMAP_BLK);
	ext4_inode_table_set(sb, &ctx->gdp, INODE_TEST_ITABLE_BLK0);

	set_opt(sb, ERRORS_CONT);
	set_opt(sb, ORDERED_DATA);
	set_opt(sb, DELALLOC);
	set_opt(sb, XATTR_USER);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	mutex_init(&sbi->s_fc_lock);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_STAGING]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_STAGING]);

	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	err = percpu_counter_init(&sbi->s_freeclusters_counter,
				  max_t(s64, 4096,
					EXT4_FREECLUSTERS_WATERMARK + 1024),
				  GFP_KERNEL);
	if (err) {
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0,
				  GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = ext4_es_register_shrinker(sbi);
	if (err) {
		percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	sbi->s_es_shrinker->count_objects = inode_test_shrinker_count;
	sbi->s_es_shrinker->scan_objects = inode_test_shrinker_scan;

	/* Initialize mock block pool */
	inode_test_setup_blk(ctx, 0, INODE_TEST_IBITMAP_BLK, true, true);
	inode_test_setup_blk(ctx, 1, INODE_TEST_ITABLE_BLK0, true, true);
	inode_test_setup_blk(ctx, 2, INODE_TEST_ITABLE_BLK1, true, true);
	inode_test_setup_blk(ctx, 3, INODE_TEST_META_BLK0, true, true);
	inode_test_setup_blk(ctx, 4, INODE_TEST_META_BLK1, true, true);
	for (i = 0; i < INODE_TEST_DATA_NBLKS; i++)
		inode_test_setup_blk(ctx, 5 + i, INODE_TEST_DATA_BLK0 + i,
				     true, true);
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0;

	/* Initialize standalone mock inode */
	inode = &ctx->ei.vfs_inode;
	init_rwsem(&ctx->ei.i_data_sem);
	inode_init_once(inode);
	inode->i_sb = sb;
	inode->i_mapping = &inode->i_data;
	inode->i_data.host = inode;
	mapping_set_gfp_mask(&inode->i_data, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&inode->i_rwsem);
	init_rwsem(&inode->i_data.invalidate_lock);
	spin_lock_init(&inode->i_lock);
	inode->__i_nlink = 1;
	atomic_set(&inode->i_count, 1);
	inode_test_init_ei_fields(&ctx->ei);

	ctx->root_dentry.d_sb = sb;
	ctx->root_dentry.d_inode = inode;

	root_inode = new_inode(sb);
	if (!root_inode) {
		ext4_es_unregister_shrinker(sbi);
		ext4_es_destroy_stats(sbi);
		percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to allocate root inode");
	}
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ext4_es_unregister_shrinker(sbi);
		ext4_es_destroy_stats(sbi);
		percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to allocate root dentry");
	}
	sb->s_flags |= SB_ACTIVE;

	up_write(&sb->s_umount);

	kunit_activate_static_stub(test, ext4_get_group_desc,
				   inode_test_get_group_desc_stub);
	kunit_activate_static_stub(test, ext4_sb_getblk,
				   inode_test_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_sb_find_get_block,
				   inode_test_sb_find_get_block_stub);
	kunit_activate_static_stub(test, ext4_sb_getblk_unmovable,
				   inode_test_sb_getblk_unmovable_stub);
	kunit_activate_static_stub(test, ext4_ext_getblk,
				   inode_test_ext_getblk_stub);
	kunit_activate_static_stub(test, ext4_read_bh_nowait,
				   inode_test_read_bh_nowait_stub);
	kunit_activate_static_stub(test, ext4_read_bh_lock,
				   inode_test_read_bh_lock_stub);
	kunit_activate_static_stub(test, ext4_sb_breadahead_unmovable,
				   inode_test_breadahead_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   inode_test_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   inode_test_free_blocks_stub);
	kunit_activate_static_stub(test, ext4_issue_zeroout,
				   inode_test_issue_zeroout_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   inode_test_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_ext_dirty,
				   inode_test_ext_dirty_stub);
	kunit_activate_static_stub(test, ext4_orphan_add,
				   inode_test_orphan_add_stub);
	kunit_activate_static_stub(test, ext4_orphan_del,
				   inode_test_orphan_del_stub);
	kunit_activate_static_stub(test, ext4_free_inode,
				   inode_test_free_inode_stub);

	err = kunit_add_action_or_reset(test, inode_test_cleanup, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);

	return ctx;
}

static struct ext4_inode *inode_test_raw_slot(struct inode_test_ctx *ctx,
					      unsigned long ino, bool disk)
{
	unsigned int grp_off = (ino - 1) % INODE_TEST_INODES_PER_GRP;
	ext4_fsblk_t blk = INODE_TEST_ITABLE_BLK0 +
			   (grp_off / INODE_TEST_INODES_PER_BLK);
	unsigned int byte_off = (grp_off % INODE_TEST_INODES_PER_BLK) *
				INODE_TEST_INODE_SIZE;
	struct inode_test_blk *slot = inode_test_find_blk(ctx, blk);
	char *base = disk ? slot->disk_buf : slot->cache_buf;

	return (struct ext4_inode *)(base + byte_off);
}

static void inode_test_stamp_raw_csum(struct inode_test_ctx *ctx,
				      unsigned long ino,
				      struct ext4_inode *raw)
{
	struct ext4_inode_info *tmp_ei = &ctx->scratch_ei;
	struct inode *tmp_inode = &tmp_ei->vfs_inode;
	__le32 inum = cpu_to_le32(ino);
	__le32 gen = raw->i_generation;
	u32 csum;

	memset(tmp_ei, 0, sizeof(*tmp_ei));
	tmp_inode->i_sb = ctx->sb;
	tmp_inode->i_ino = ino;
	tmp_ei->i_extra_isize = le16_to_cpu(raw->i_extra_isize);
	csum = ext4_chksum(ctx->sbi.s_csum_seed, (u8 *)&inum, sizeof(inum));
	tmp_ei->i_csum_seed = ext4_chksum(csum, (u8 *)&gen, sizeof(gen));
	ext4_inode_csum_set(tmp_inode, raw, tmp_ei);
}

static void inode_test_prepare_valid_raw_inode(struct inode_test_ctx *ctx,
					       unsigned long ino, umode_t mode)
{
	struct inode_test_blk *slot;
	struct ext4_inode *raw;
	struct ext4_extent_header *eh;
	unsigned int grp_off = (ino - 1) % INODE_TEST_INODES_PER_GRP;
	ext4_fsblk_t blk = INODE_TEST_ITABLE_BLK0 +
			   (grp_off / INODE_TEST_INODES_PER_BLK);

	slot = inode_test_find_blk(ctx, blk);
	clear_buffer_uptodate(&slot->bh);
	raw = inode_test_raw_slot(ctx, ino, true);
	memset(raw, 0, INODE_TEST_INODE_SIZE);

	raw->i_mode = cpu_to_le16(mode);
	raw->i_links_count = cpu_to_le16(1);
	raw->i_extra_isize = cpu_to_le16(INODE_TEST_EXTRA_ISIZE);
	raw->i_generation = cpu_to_le32(7);
	if (S_ISREG(mode) || S_ISDIR(mode)) {
		raw->i_flags = cpu_to_le32(EXT4_EXTENTS_FL);
		eh = (struct ext4_extent_header *)raw->i_block;
		eh->eh_magic = EXT4_EXT_MAGIC;
		eh->eh_entries = 0;
		eh->eh_max = cpu_to_le16(4);
		eh->eh_depth = 0;
	}
	inode_test_stamp_raw_csum(ctx, ino, raw);
}

/* -------------------------------------------------------------------------
 * 1. Timestamp Encoding & Macro Round-Trip Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_xtimestamp_encoding_roundtrip(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct ext4_inode_info *ei = &ctx->ei;
	struct inode *inode = &ei->vfs_inode;
	struct ext4_inode *raw = &ctx->scratch_raw.raw;
	struct timespec64 ts_in, ts_out;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(test_data); i++) {
		__le32 encoded_extra = ext4_encode_extra_time(test_data[i].expected);
		__le32 encoded_base = cpu_to_le32((s32)test_data[i].expected.tv_sec);

		KUNIT_EXPECT_EQ_MSG(test, le32_to_cpu(encoded_extra),
				    test_data[i].extra_bits,
				    "%s", test_data[i].test_case_name);
		ts_out = ext4_decode_extra_time(encoded_base, encoded_extra);
		KUNIT_EXPECT_EQ(test, ts_out.tv_sec,
				test_data[i].expected.tv_sec);
		KUNIT_EXPECT_EQ(test, ts_out.tv_nsec,
				test_data[i].expected.tv_nsec);
	}

	/* 256-byte inode with extra_isize = 32: full 34-bit + nsec round-trip */
	ts_in.tv_sec = 0x240001234LL;
	ts_in.tv_nsec = 987654321L;
	inode_set_atime_to_ts(inode, ts_in);
	ts_in.tv_sec = 0x190005678LL;
	ts_in.tv_nsec = 123456789L;
	inode_set_mtime_to_ts(inode, ts_in);
	ts_in.tv_sec = 0x310009abcLL;
	ts_in.tv_nsec = 555666777L;
	inode_set_ctime_to_ts(inode, ts_in);
	ei->i_crtime.tv_sec = 0x22000def0LL;
	ei->i_crtime.tv_nsec = 444333222L;

	EXT4_INODE_SET_ATIME(inode, raw);
	EXT4_INODE_SET_MTIME(inode, raw);
	EXT4_INODE_SET_CTIME(inode, raw);
	EXT4_EINODE_SET_XTIME(i_crtime, ei, raw);

	inode_set_atime(inode, 0, 0);
	inode_set_mtime(inode, 0, 0);
	inode_set_ctime(inode, 0, 0);
	ei->i_crtime = (struct timespec64){};

	EXT4_INODE_GET_ATIME(inode, raw);
	EXT4_INODE_GET_MTIME(inode, raw);
	EXT4_INODE_GET_CTIME(inode, raw);
	EXT4_EINODE_GET_XTIME(i_crtime, ei, raw);

	KUNIT_EXPECT_EQ(test, inode_get_atime_sec(inode), 0x240001234LL);
	KUNIT_EXPECT_EQ(test, inode_get_atime_nsec(inode), 987654321L);
	KUNIT_EXPECT_EQ(test, inode_get_mtime_sec(inode), 0x190005678LL);
	KUNIT_EXPECT_EQ(test, inode_get_mtime_nsec(inode), 123456789L);
	KUNIT_EXPECT_EQ(test, inode_get_ctime_sec(inode), 0x310009abcLL);
	KUNIT_EXPECT_EQ(test, inode_get_ctime_nsec(inode), 555666777L);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_crtime.tv_sec, 0x22000def0LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_crtime.tv_nsec, 444333222L);

	/* 128-byte old inode: 32-bit signed seconds only, extra ignored, nsec=0 */
	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	ctx->ei.i_extra_isize = 0;
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);

	inode_set_mtime(inode, S32_MAX, 500);
	inode_set_atime(inode, S32_MIN, 700);
	EXT4_INODE_SET_MTIME(inode, raw);
	EXT4_INODE_SET_ATIME(inode, raw);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_mtime_extra), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_atime_extra), 0U);

	inode_set_mtime(inode, 0, 999);
	inode_set_atime(inode, 0, 999);
	EXT4_INODE_GET_MTIME(inode, raw);
	EXT4_INODE_GET_ATIME(inode, raw);

	KUNIT_EXPECT_EQ(test, inode_get_mtime_sec(inode), (time64_t)S32_MAX);
	KUNIT_EXPECT_EQ(test, inode_get_mtime_nsec(inode), 0L);
	KUNIT_EXPECT_EQ(test, inode_get_atime_sec(inode), (time64_t)S32_MIN);
	KUNIT_EXPECT_EQ(test, inode_get_atime_nsec(inode), 0L);

	ctx->sbi.s_inode_size = INODE_TEST_INODE_SIZE;
	ctx->ei.i_extra_isize = INODE_TEST_EXTRA_ISIZE;
}

/* -------------------------------------------------------------------------
 * 2. Inode Checksum Calculation & Verification Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_csum_set_and_verify(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_inode *raw = &ctx->scratch_raw.raw;

	raw->i_mode = cpu_to_le16(S_IFREG | 0644);
	raw->i_links_count = cpu_to_le16(1);
	raw->i_extra_isize = cpu_to_le16(INODE_TEST_EXTRA_ISIZE);

	/* Full 32-bit checksum (256B inode, extra_isize = 32) */
	ext4_inode_csum_set(inode, raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);

	/* Bit flip in low 16 bits fails */
	raw->i_checksum_lo ^= cpu_to_le16(0x0001);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);
	raw->i_checksum_lo ^= cpu_to_le16(0x0001);

	/* Bit flip in high 16 bits fails */
	raw->i_checksum_hi ^= cpu_to_le16(0x8000);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);
	raw->i_checksum_hi ^= cpu_to_le16(0x8000);

	/* Bit flip in raw inode body and in-body xattr tail (byte 200) fails */
	raw->i_mode ^= cpu_to_le16(0x0002);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);
	raw->i_mode ^= cpu_to_le16(0x0002);
	ctx->scratch_raw.bytes[200] ^= 0x01;
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);
	ctx->scratch_raw.bytes[200] ^= 0x01;
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);

	/* Non-Linux creator OS bypasses checksum verification */
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_HURD);
	raw->i_checksum_lo ^= cpu_to_le16(0xffff);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);

	/* Disabled metadata_csum feature bypasses checksum verification */
	ext4_clear_feature_metadata_csum(ctx->sb);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);
	ext4_set_feature_metadata_csum(ctx->sb);

	/* 256B inode with extra_isize = 0 (excludes i_checksum_hi): 16-bit */
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);
	ctx->ei.i_extra_isize = 0;
	raw->i_extra_isize = 0;
	raw->i_checksum_hi = cpu_to_le16(0xbeef);
	ext4_inode_csum_set(inode, raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_checksum_hi), 0xbeef);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);
	raw->i_checksum_lo ^= cpu_to_le16(0x0010);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);

	/* 256B inode with extra_isize = 4 (includes i_checksum_hi): 32-bit */
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);
	ctx->ei.i_extra_isize = 4;
	raw->i_extra_isize = cpu_to_le16(4);
	ext4_inode_csum_set(inode, raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);
	raw->i_checksum_hi ^= cpu_to_le16(0x0001);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);

	/* 128B old inode: 16-bit checksum in i_checksum_lo only */
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);
	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	ctx->ei.i_extra_isize = 0;
	ext4_inode_csum_set(inode, raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);
	raw->i_checksum_lo ^= cpu_to_le16(0x0100);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			0);
}

/* -------------------------------------------------------------------------
 * 3. i_blocks Encoding & Decoding Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_blocks_encode_decode(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_inode *raw = &ctx->scratch_raw.raw;
	blkcnt_t large_sectors;
	int err;

	/* 32-bit sector count clears EXT4_INODE_HUGE_FILE */
	ext4_set_inode_flag(inode, EXT4_INODE_HUGE_FILE);
	inode->i_blocks = 0x12345678ULL;
	err = ext4_inode_blocks_set_test(raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_HUGE_FILE));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_blocks_lo), 0x12345678U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_blocks_high), 0);
	KUNIT_EXPECT_EQ(test, ext4_inode_blocks_test(raw, &ctx->ei),
			(blkcnt_t)0x12345678ULL);

	/* > 32-bit without huge_file feature fails with -EFSCORRUPTED */
	ext4_clear_feature_huge_file(ctx->sb);
	inode->i_blocks = 0x100000000ULL;
	err = ext4_inode_blocks_set_test(raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);
	ext4_set_feature_huge_file(ctx->sb);

	/* 48-bit sector count with huge_file feature (still sector units) */
	ext4_set_inode_flag(inode, EXT4_INODE_HUGE_FILE);
	inode->i_blocks = 0xabcd12345678ULL;
	err = ext4_inode_blocks_set_test(raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_HUGE_FILE));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_blocks_lo), 0x12345678U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_blocks_high), 0xabcd);
	KUNIT_EXPECT_EQ(test, ext4_inode_blocks_test(raw, &ctx->ei),
			(blkcnt_t)0xabcd12345678ULL);

	/* > 48-bit sector count scales by fs-block units and sets HUGE_FILE */
	large_sectors = 0x0002000000000008ULL; /* multiple of 8 (4K / 512B) */
	inode->i_blocks = large_sectors;
	err = ext4_inode_blocks_set_test(raw, &ctx->ei);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_flag(inode, EXT4_INODE_HUGE_FILE));
	KUNIT_EXPECT_EQ(test, ext4_inode_blocks_test(raw, &ctx->ei),
			large_sectors);
}

/* -------------------------------------------------------------------------
 * 4. Raw Inode Serialization (ext4_fill_raw_inode) Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_fill_raw_inode(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_inode *raw = &ctx->scratch_raw.raw;
	dev_t small_dev, large_dev;
	int err;

	/* 32-bit uid/gid, 48-bit file_acl, 64-bit disksize & iversion, projid */
	ext4_set_feature_project(ctx->sb);
	i_uid_write(inode, 0x12345678U);
	i_gid_write(inode, 0x9abcdef0U);
	ctx->ei.i_projid = make_kprojid(&init_user_ns, 0x55aa1122U);
	ctx->ei.i_file_acl = 0x000344556677ULL;
	ctx->ei.i_disksize = 0x0000000511223344LL;
	inode_set_iversion_queried(inode, 0x000000078899aabbULL);

	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_low), 0x5678);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_high), 0x1234);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_gid_low), 0xdef0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_gid_high), 0x9abc);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_projid), 0x55aa1122U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_file_acl_lo), 0x44556677U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_file_acl_high), 0x0003);
	KUNIT_EXPECT_EQ(test, ext4_isize(ctx->sb, raw), 0x0000000511223344LL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_disk_version), 0x8899aabbU);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_version_hi), 0x00000007U);
	KUNIT_EXPECT_EQ(test, ext4_inode_csum_verify_test(inode, raw, &ctx->ei),
			1);

	/* Non-default projid without PROJECT feature returns -EFSCORRUPTED */
	ext4_clear_feature_project(ctx->sb);
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);
	ctx->ei.i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	/* NO_UID32 masks uid/gid high bits; HURD_COMPAT skips file_acl_high */
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);
	raw->osd2.hurd2.h_i_mode_high = 0x7777;
	set_opt(ctx->sb, NO_UID32);
	set_opt2(ctx->sb, HURD_COMPAT);
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_high), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_gid_high), 0);
	KUNIT_EXPECT_EQ(test, raw->osd2.hurd2.h_i_mode_high, 0x7777);
	clear_opt(ctx->sb, NO_UID32);
	clear_opt2(ctx->sb, HURD_COMPAT);

	/* Deleted inode (i_dtime != 0) clears uid_high/gid_high unless orphan */
	ctx->ei.i_dtime = 100;
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_high), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_gid_high), 0);

	ext4_set_inode_state(inode, EXT4_STATE_ORPHAN_FILE);
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_high), 0x1234);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_gid_high), 0x9abc);
	ext4_clear_inode_state(inode, EXT4_STATE_ORPHAN_FILE);
	ctx->ei.i_dtime = 0;

	/* EA_INODE stores raw iversion without shifting out flag bit */
	ext4_set_inode_flag(inode, EXT4_INODE_EA_INODE);
	inode_set_iversion_raw(inode, 0x00000002cafebabeULL);
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_disk_version), 0xcafebabeU);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_version_hi), 0x00000002U);
	ext4_clear_inode_flag(inode, EXT4_INODE_EA_INODE);

	/* Inline-data inode preserves raw_inode->i_block contents */
	memset(ctx->scratch_raw.bytes, 0, INODE_TEST_INODE_SIZE);
	raw->i_block[0] = cpu_to_le32(0xdeadbeefU);
	ctx->ei.i_data[0] = cpu_to_le32(0x11111111U);
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_block[0]), 0xdeadbeefU);
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	ctx->ei.i_inline_off = 0;

	/* Special device inode encodes old vs new rdev into i_block[0]/[1] */
	inode->i_mode = S_IFCHR | 0600;
	small_dev = MKDEV(8, 3);
	inode->i_rdev = small_dev;
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, old_decode_dev(le32_to_cpu(raw->i_block[0])),
			small_dev);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_block[1]), 0U);

	large_dev = MKDEV(512, 1024);
	inode->i_rdev = large_dev;
	err = ext4_fill_raw_inode_test(inode, raw);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_block[0]), 0U);
	KUNIT_EXPECT_EQ(test, new_decode_dev(le32_to_cpu(raw->i_block[1])),
			large_dev);
}

/* -------------------------------------------------------------------------
 * 5. Inode Dirtying, LARGE_FILE Promotion, and Lazytime Neighbor Flush
 * -------------------------------------------------------------------------
 */

static void inode_test_mark_dirty_and_update(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *itable = inode_test_find_blk(ctx,
							   INODE_TEST_ITABLE_BLK0);
	struct ext4_inode *raw;
	struct ext4_inode *neighbor_raw;
	struct inode *neighbor;
	struct ext4_iloc iloc = {};
	handle_t *handle;
	int ref_before, err;

	/*
	 * 1. EXT4_STATE_NEW + LARGE_FILE feature promotion in
	 *    ext4_reserve_inode_write() / ext4_mark_iloc_dirty().
	 */
	ext4_clear_feature_large_file(ctx->sb);
	ext4_set_inode_state(inode, EXT4_STATE_NEW);
	ctx->ei.i_disksize = 0x100000000LL;
	inode->i_size = ctx->ei.i_disksize;
	raw = inode_test_raw_slot(ctx, INODE_TEST_PRIMARY_INO, false);
	memset(raw, 0xff, INODE_TEST_INODE_SIZE);

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	err = ext4_reserve_inode_write(handle, inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	err = ext4_mark_iloc_dirty(handle, inode, &iloc);
	ext4_journal_stop(handle);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, ext4_test_inode_state(inode, EXT4_STATE_NEW));
	KUNIT_EXPECT_TRUE(test, ext4_has_feature_large_file(ctx->sb));
	KUNIT_EXPECT_EQ(test, ext4_isize(ctx->sb, raw), 0x100000000LL);
	KUNIT_EXPECT_TRUE(test,
			  !!(inode_state_read_once(inode) & I_METADATA_WRITEBACK));

	/*
	 * 2. SB_LAZYTIME opportunistic neighbor timestamp flush + automatic
	 *    extra_isize expansion via __ext4_mark_inode_dirty().
	 */
	inode_test_prepare_valid_raw_inode(ctx, 13, S_IFREG | 0644);
	memcpy(itable->cache_buf, itable->disk_buf, INODE_TEST_BLK_SIZE);
	set_buffer_uptodate(&itable->bh);

	neighbor = ext4_iget(ctx->sb, 13, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(neighbor));
	inode_test_track_iget_inode(test, ctx, neighbor);

	inode_set_mtime(neighbor, 0x55667788LL, 1234);
	spin_lock(&neighbor->i_lock);
	inode_state_set(neighbor, I_DIRTY_TIME);
	spin_unlock(&neighbor->i_lock);

	ctx->sb->s_flags |= SB_LAZYTIME;
	ctx->ei.i_extra_isize = 16;
	ctx->sbi.s_want_extra_isize = 32;

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	err = __ext4_mark_inode_dirty(handle, inode, __func__, __LINE__);
	ext4_journal_stop(handle);
	ctx->sb->s_flags &= ~SB_LAZYTIME;

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_extra_isize, (u16)32);
	KUNIT_EXPECT_FALSE(test,
			   !!(inode_state_read_once(neighbor) & I_DIRTY_TIME));
	neighbor_raw = inode_test_raw_slot(ctx, 13, false);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(neighbor_raw->i_mtime), 0x55667788U);
	KUNIT_EXPECT_EQ(test,
			ext4_inode_csum_verify_test(neighbor, neighbor_raw,
						    EXT4_I(neighbor)), 1);

	/* 3. ext4_dirty_inode() updates raw inode on I_DIRTY_SYNC */
	inode_set_ctime(inode, 0x12340000LL, 0);
	ext4_dirty_inode(inode, I_DIRTY_SYNC);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_ctime), 0x12340000U);

	/* 4. Emergency RO state returns -EROFS and drops iloc.bh cleanly */
	err = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	ref_before = atomic_read(&itable->bh.b_count);
	set_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	err = ext4_mark_iloc_dirty(NULL, inode, &iloc);
	clear_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, err, -EROFS);
	KUNIT_EXPECT_EQ(test, atomic_read(&itable->bh.b_count), ref_before - 1);
}

/* -------------------------------------------------------------------------
 * 6. Extra Isize Expansion Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_expand_extra_isize(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_iloc iloc = {};
	struct ext4_inode *raw;
	struct ext4_xattr_ibody_header *ihdr;
	struct ext4_xattr_entry *entry;
	int no_expand = 0;
	int err;

	err = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	raw = ext4_raw_inode(&iloc);

	/* Corrupted current i_extra_isize (unaligned or oversize) */
	ctx->ei.i_extra_isize = 13;
	err = ext4_expand_extra_isize_test(inode, 32, &iloc, NULL, &no_expand);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);

	ctx->ei.i_extra_isize = 200;
	err = ext4_expand_extra_isize_test(inode, 204, &iloc, NULL, &no_expand);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);

	/* Invalid target new_extra_isize */
	ctx->ei.i_extra_isize = 16;
	KUNIT_EXPECT_EQ(test,
			ext4_expand_extra_isize_test(inode, 12, &iloc, NULL,
						     &no_expand), -EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_expand_extra_isize_test(inode, 200, &iloc, NULL,
						     &no_expand), -EINVAL);

	/* Clean expansion without xattrs zeroes newly covered extra space */
	memset((u8 *)raw + EXT4_GOOD_OLD_INODE_SIZE + 16, 0xaa, 32);
	err = ext4_expand_extra_isize_test(inode, 32, &iloc, NULL, &no_expand);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_extra_isize, (u16)32);
	KUNIT_EXPECT_EQ(test, *((u8 *)raw + EXT4_GOOD_OLD_INODE_SIZE + 16), 0);
	KUNIT_EXPECT_EQ(test, *((u8 *)raw + EXT4_GOOD_OLD_INODE_SIZE + 31), 0);
	KUNIT_EXPECT_EQ(test, *((u8 *)raw + EXT4_GOOD_OLD_INODE_SIZE + 32),
			0xaa);

	/* Guard checks in ext4_try_to_expand_extra_isize() */
	ctx->ei.i_extra_isize = 16;
	ext4_set_inode_state(inode, EXT4_STATE_NO_EXPAND);
	KUNIT_EXPECT_EQ(test,
			ext4_try_to_expand_extra_isize_test(inode, 32, &iloc,
							    NULL), -EOVERFLOW);
	ext4_clear_inode_state(inode, EXT4_STATE_NO_EXPAND);

	ctx->sb->s_flags &= ~SB_ACTIVE;
	KUNIT_EXPECT_EQ(test,
			ext4_try_to_expand_extra_isize_test(inode, 32, &iloc,
							    NULL), -EBUSY);
	ctx->sb->s_flags |= SB_ACTIVE;

	down_read(&ctx->ei.xattr_sem);
	err = ext4_try_to_expand_extra_isize_test(inode, 32, &iloc, NULL);
	up_read(&ctx->ei.xattr_sem);
	KUNIT_EXPECT_EQ(test, err, -EBUSY);

	/* Expansion with valid in-body xattr shifts entry forward cleanly */
	memset(raw, 0, INODE_TEST_INODE_SIZE);
	ctx->ei.i_extra_isize = 16;
	raw->i_extra_isize = cpu_to_le16(16);
	ext4_set_inode_state(inode, EXT4_STATE_XATTR);
	ihdr = IHDR(inode, raw);
	ihdr->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
	entry = IFIRST(ihdr);
	entry->e_name_len = 1;
	entry->e_name_index = EXT4_XATTR_INDEX_USER;
	entry->e_name[0] = 'k';
	entry->e_value_size = cpu_to_le32(4);
	entry->e_value_offs = cpu_to_le16(INODE_TEST_INODE_SIZE -
					  EXT4_GOOD_OLD_INODE_SIZE - 16 -
					  sizeof(__u32) - 4);
	memcpy((char *)IFIRST(ihdr) + le16_to_cpu(entry->e_value_offs),
	       "val!", 4);
	inode_test_stamp_raw_csum(ctx, inode->i_ino, raw);

	/* ext4_expand_extra_isize() locks xattr_sem and consumes iloc.bh */
	err = ext4_expand_extra_isize(inode, 32, &iloc);
	ctx->sb->s_flags &= ~SB_ACTIVE;

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_extra_isize, (u16)32);
	ihdr = IHDR(inode, raw);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ihdr->h_magic),
			(u32)EXT4_XATTR_MAGIC);
}

/* -------------------------------------------------------------------------
 * 7. Inode Location Lookup (get_inode_loc / noio / fc_inode_loc) Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_get_inode_loc_variants(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *itable = inode_test_find_blk(ctx,
							   INODE_TEST_ITABLE_BLK0);
	struct inode_test_blk *ibitmap = inode_test_find_blk(ctx,
							    INODE_TEST_IBITMAP_BLK);
	struct ext4_iloc iloc = {};
	struct ext4_inode *raw;
	int err;

	/* Out-of-range inode numbers */
	inode->i_ino = 1;
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc(inode, &iloc), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc_noio(inode, &iloc),
			-EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, ext4_get_fc_inode_loc(ctx->sb, 99999, &iloc),
			-EFSCORRUPTED);
	inode->i_ino = INODE_TEST_PRIMARY_INO;

	/* Missing group descriptor -> -EIO */
	ctx->fail_group_desc = true;
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc(inode, &iloc), -EIO);
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc_noio(inode, &iloc), -EIO);
	ctx->fail_group_desc = false;

	/* Invalid inode table block in group descriptor -> -EFSCORRUPTED */
	ext4_inode_table_set(ctx->sb, &ctx->gdp, 1);
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc(inode, &iloc), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc_noio(inode, &iloc),
			-EFSCORRUPTED);
	ext4_inode_table_set(ctx->sb, &ctx->gdp, INODE_TEST_ITABLE_BLK0);

	/* noio variant: uncached -> -EAGAIN, non-uptodate -> -EAGAIN, hit -> 0 */
	itable->in_cache = false;
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc_noio(inode, &iloc), -EAGAIN);
	itable->in_cache = true;
	clear_buffer_uptodate(&itable->bh);
	KUNIT_EXPECT_EQ(test, ext4_get_inode_loc_noio(inode, &iloc), -EAGAIN);
	KUNIT_EXPECT_EQ(test, atomic_read(&itable->bh.b_count), 1);

	set_buffer_uptodate(&itable->bh);
	err = ext4_get_inode_loc_noio(inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, iloc.offset,
			(unsigned long)((INODE_TEST_PRIMARY_INO - 1) *
					INODE_TEST_INODE_SIZE));
	brelse(iloc.bh);

	/*
	 * Skip-I/O optimization: non-uptodate itable block + uptodate inode
	 * bitmap with ONLY primary inode bit set -> zero I/O reads!
	 */
	clear_buffer_uptodate(&itable->bh);
	memset(itable->disk_buf, 0x77, INODE_TEST_BLK_SIZE);
	memset(ibitmap->cache_buf, 0, INODE_TEST_BLK_SIZE);
	ext4_set_bit(INODE_TEST_PRIMARY_INO - 1, ibitmap->cache_buf);
	set_buffer_uptodate(&ibitmap->bh);
	ctx->read_nowait_calls = 0;
	i_uid_write(inode, 0x4242);

	err = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 0U);
	raw = ext4_raw_inode(&iloc);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_low), 0x4242);
	/* Neighbor slot was zeroed rather than read from 0x77 disk_buf */
	KUNIT_EXPECT_EQ(test, (u8)itable->cache_buf[0], 0);
	brelse(iloc.bh);

	/*
	 * Neighbor inode bit also set in bitmap -> cannot skip I/O; performs
	 * disk read and triggers inode table readahead window calculation.
	 */
	clear_buffer_uptodate(&itable->bh);
	ext4_set_bit(INODE_TEST_PRIMARY_INO, ibitmap->cache_buf);
	ctx->sbi.s_inode_readahead_blks = 4;
	ctx->read_nowait_calls = 0;
	ctx->readahead_calls = 0;

	err = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	KUNIT_EXPECT_GT(test, ctx->readahead_calls, 0U);
	KUNIT_EXPECT_EQ(test, ctx->first_readahead_blk,
			(ext4_fsblk_t)INODE_TEST_ITABLE_BLK0);
	KUNIT_EXPECT_EQ(test, (u8)itable->cache_buf[0], 0x77);
	brelse(iloc.bh);
	ctx->sbi.s_inode_readahead_blks = 0;

	/* Disk read -EIO propagates cleanly with balanced bh refcount */
	clear_buffer_uptodate(&itable->bh);
	ctx->fail_read_eio = true;
	err = ext4_get_fc_inode_loc(ctx->sb, INODE_TEST_PRIMARY_INO, &iloc);
	ctx->fail_read_eio = false;
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&itable->bh.b_count), 1);
}

/* -------------------------------------------------------------------------
 * 8. __ext4_iget() Valid Modes, Caching, & Special Inode Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_iget_validation_and_modes(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct ext4_inode *raw;
	struct inode *inode, *cached;

	/* Illegal & special ino checks before VFS lookup */
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_iget(ctx->sb, 0, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_iget(ctx->sb, 0, EXT4_IGET_HANDLE)),
			-ESTALE);
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_iget(ctx->sb, 8, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_iget(ctx->sb, 99999, EXT4_IGET_HANDLE)),
			-ESTALE);

	/* EXT4_IGET_NOWAIT returns -ENOENT when inode is not cached */
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_iget(ctx->sb, 14, EXT4_IGET_NOWAIT)),
			-ENOENT);

	/* Valid regular file inode load & cached NOWAIT / check_igot_inode */
	ext4_set_feature_project(ctx->sb);
	inode_test_prepare_valid_raw_inode(ctx, 14, S_IFREG | 0640);
	raw = inode_test_raw_slot(ctx, 14, true);
	raw->i_uid_low = cpu_to_le16(0x2222);
	raw->i_uid_high = cpu_to_le16(0x1111);
	raw->i_gid_low = cpu_to_le16(0x4444);
	raw->i_gid_high = cpu_to_le16(0x3333);
	raw->i_projid = cpu_to_le32(77);
	raw->i_size_lo = cpu_to_le32(8192);
	raw->i_disk_version = cpu_to_le32(9);
	raw->i_version_hi = cpu_to_le32(2);
	inode_test_stamp_raw_csum(ctx, 14, raw);

	inode = ext4_iget(ctx->sb, 14, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);
	KUNIT_EXPECT_EQ(test, i_uid_read(inode), 0x11112222U);
	KUNIT_EXPECT_EQ(test, i_gid_read(inode), 0x33334444U);
	KUNIT_EXPECT_EQ(test, from_kprojid(&init_user_ns, EXT4_I(inode)->i_projid),
			77U);
	KUNIT_EXPECT_EQ(test, i_size_read(inode), 8192LL);
	KUNIT_EXPECT_EQ(test, inode_peek_iversion(inode),
			(0x2ULL << 32) | 9ULL);

	cached = ext4_iget(ctx->sb, 14, EXT4_IGET_NOWAIT);
	KUNIT_ASSERT_PTR_EQ(test, cached, inode);
	iput(cached);

	/* Cached non-EA inode rejected when EXT4_IGET_EA_INODE requested */
	cached = ext4_iget(ctx->sb, 14, EXT4_IGET_EA_INODE);
	KUNIT_EXPECT_EQ(test, PTR_ERR(cached), -EFSCORRUPTED);

	/* Valid fast symlink sets cached i_link */
	inode_test_prepare_valid_raw_inode(ctx, 15, S_IFLNK | 0777);
	raw = inode_test_raw_slot(ctx, 15, true);
	raw->i_flags = 0;
	raw->i_size_lo = cpu_to_le32(11);
	memcpy(raw->i_block, "target/path", 11);
	inode_test_stamp_raw_csum(ctx, 15, raw);

	inode = ext4_iget(ctx->sb, 15, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);
	KUNIT_EXPECT_STREQ(test, inode->i_link, "target/path");

	/* Valid character & block device special inodes (old & new rdev) */
	inode_test_prepare_valid_raw_inode(ctx, 16, S_IFCHR | 0600);
	raw = inode_test_raw_slot(ctx, 16, true);
	raw->i_block[0] = cpu_to_le32(old_encode_dev(MKDEV(4, 64)));
	inode_test_stamp_raw_csum(ctx, 16, raw);

	inode = ext4_iget(ctx->sb, 16, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);
	KUNIT_EXPECT_EQ(test, inode->i_rdev, MKDEV(4, 64));

	inode_test_prepare_valid_raw_inode(ctx, 17, S_IFBLK | 0600);
	raw = inode_test_raw_slot(ctx, 17, true);
	raw->i_block[0] = 0;
	raw->i_block[1] = cpu_to_le32(new_encode_dev(MKDEV(300, 700)));
	inode_test_stamp_raw_csum(ctx, 17, raw);

	inode = ext4_iget(ctx->sb, 17, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);
	KUNIT_EXPECT_EQ(test, inode->i_rdev, MKDEV(300, 700));

	/* Uninitialized EXT4_BOOT_LOADER_INO requires EXT4_IGET_BAD */
	inode_test_prepare_valid_raw_inode(ctx, EXT4_BOOT_LOADER_INO, 0);
	raw = inode_test_raw_slot(ctx, EXT4_BOOT_LOADER_INO, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, EXT4_BOOT_LOADER_INO, raw);

	inode = ext4_iget(ctx->sb, EXT4_BOOT_LOADER_INO, EXT4_IGET_SPECIAL);
	KUNIT_EXPECT_EQ(test, PTR_ERR(inode), -EFSCORRUPTED);

	inode_test_prepare_valid_raw_inode(ctx, EXT4_BOOT_LOADER_INO, 0);
	raw = inode_test_raw_slot(ctx, EXT4_BOOT_LOADER_INO, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, EXT4_BOOT_LOADER_INO, raw);

	inode = ext4_iget(ctx->sb, EXT4_BOOT_LOADER_INO,
			  EXT4_IGET_SPECIAL | EXT4_IGET_BAD);
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);
	KUNIT_EXPECT_TRUE(test, is_bad_inode(inode));
	cached = ext4_iget(ctx->sb, EXT4_BOOT_LOADER_INO, EXT4_IGET_SPECIAL);
	KUNIT_EXPECT_EQ(test, PTR_ERR(cached), -EFSCORRUPTED);
}

/* -------------------------------------------------------------------------
 * 9. __ext4_iget() On-Disk Corruption Rejection Matrix
 * -------------------------------------------------------------------------
 */

static void inode_test_iget_corruptions(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct ext4_inode *raw;
	struct inode *inode;

	/* 1. Unallocated inode with EXT4_IGET_HANDLE -> -ESTALE */
	inode_test_prepare_valid_raw_inode(ctx, 18, 0);
	raw = inode_test_raw_slot(ctx, 18, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, 18, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 18, EXT4_IGET_HANDLE)),
			-ESTALE);

	/* 2. Unaligned i_extra_isize -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 19, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 19, true);
	raw->i_extra_isize = cpu_to_le16(5);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 19, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 3. Oversize i_extra_isize -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 20, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 20, true);
	raw->i_extra_isize = cpu_to_le16(132);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 20, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 4. Bad checksum -> -EFSBADCRC (and EXT4_FC_REPLAY bypasses it) */
	inode_test_prepare_valid_raw_inode(ctx, 21, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 21, true);
	raw->i_checksum_lo ^= cpu_to_le16(0x00ff);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 21, EXT4_IGET_NORMAL)),
			-EFSBADCRC);

	inode_test_prepare_valid_raw_inode(ctx, 21, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 21, true);
	raw->i_checksum_lo ^= cpu_to_le16(0x00ff);
	ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;
	inode = ext4_iget(ctx->sb, 21, EXT4_IGET_NORMAL);
	ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_track_iget_inode(test, ctx, inode);

	/* 5. Deleted inode (nlink=0) -> -ESTALE normally, allowed in ORPHAN_FS */
	inode_test_prepare_valid_raw_inode(ctx, 22, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 22, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, 22, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 22, EXT4_IGET_NORMAL)),
			-ESTALE);

	inode_test_prepare_valid_raw_inode(ctx, 22, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 22, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, 22, raw);
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;
	inode = ext4_iget(ctx->sb, 22, EXT4_IGET_NORMAL);
	ctx->sbi.s_mount_state &= ~EXT4_ORPHAN_FS;
	KUNIT_ASSERT_FALSE(test, IS_ERR(inode));
	inode_test_put_tracked_inode(ctx, inode);

	/* 6. Unallocated special inode -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 7, S_IFREG | 0600);
	raw = inode_test_raw_slot(ctx, 7, true);
	raw->i_links_count = 0;
	inode_test_stamp_raw_csum(ctx, 7, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 7, EXT4_IGET_SPECIAL)),
			-EFSCORRUPTED);

	/* 7. Conflicting INLINE_DATA | EXTENTS flags -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 23, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 23, true);
	raw->i_flags = cpu_to_le32(EXT4_INLINE_DATA_FL | EXT4_EXTENTS_FL);
	inode_test_stamp_raw_csum(ctx, 23, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 23, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 8. Indirect file i_size exceeding s_bitmap_maxbytes -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 24, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 24, true);
	raw->i_flags = 0;
	memset(raw->i_block, 0, sizeof(raw->i_block));
	raw->i_size_high = cpu_to_le32(0x1000);
	inode_test_stamp_raw_csum(ctx, 24, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 24, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 9. Directory with INDEX flag when dir_index disabled -> -EFSCORRUPTED */
	ext4_clear_feature_dir_index(ctx->sb);
	inode_test_prepare_valid_raw_inode(ctx, 25, S_IFDIR | 0755);
	raw = inode_test_raw_slot(ctx, 25, true);
	raw->i_flags |= cpu_to_le32(EXT4_INDEX_FL);
	inode_test_stamp_raw_csum(ctx, 25, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 25, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);
	ext4_set_feature_dir_index(ctx->sb);

	/* 10. Out-of-range i_file_acl block -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 26, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 26, true);
	raw->i_file_acl_lo = cpu_to_le32(INODE_TEST_TOTAL_BLKS + 100);
	inode_test_stamp_raw_csum(ctx, 26, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 26, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 11. Corrupted extent header magic -> -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 27, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 27, true);
	raw->i_block[0] = 0;
	inode_test_stamp_raw_csum(ctx, 27, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 27, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 12. Symlink with IMMUTABLE flag & fast symlink with embedded NUL */
	inode_test_prepare_valid_raw_inode(ctx, 28, S_IFLNK | 0777);
	raw = inode_test_raw_slot(ctx, 28, true);
	raw->i_flags = cpu_to_le32(EXT4_IMMUTABLE_FL);
	raw->i_size_lo = cpu_to_le32(3);
	memcpy(raw->i_block, "abc", 3);
	inode_test_stamp_raw_csum(ctx, 28, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 28, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	inode_test_prepare_valid_raw_inode(ctx, 28, S_IFLNK | 0777);
	raw = inode_test_raw_slot(ctx, 28, true);
	raw->i_flags = 0;
	raw->i_size_lo = cpu_to_le32(5);
	memcpy(raw->i_block, "ab\0cd", 5);
	inode_test_stamp_raw_csum(ctx, 28, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 28, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 13. Bogus i_mode (0) with nlink > 0 & CASEFOLD without feature */
	inode_test_prepare_valid_raw_inode(ctx, 29, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 29, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	inode_test_prepare_valid_raw_inode(ctx, 30, S_IFDIR | 0755);
	raw = inode_test_raw_slot(ctx, 30, true);
	raw->i_flags |= cpu_to_le32(EXT4_CASEFOLD_FL);
	inode_test_stamp_raw_csum(ctx, 30, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 30, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);

	/* 14. EA_INODE opened without EXT4_IGET_EA_INODE: -ESTALE vs -EFSCORRUPTED */
	inode_test_prepare_valid_raw_inode(ctx, 31, S_IFREG | 0600);
	raw = inode_test_raw_slot(ctx, 31, true);
	raw->i_flags |= cpu_to_le32(EXT4_EA_INODE_FL);
	inode_test_stamp_raw_csum(ctx, 31, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 31, EXT4_IGET_HANDLE)),
			-ESTALE);

	inode_test_prepare_valid_raw_inode(ctx, 31, S_IFREG | 0600);
	raw = inode_test_raw_slot(ctx, 31, true);
	raw->i_flags |= cpu_to_le32(EXT4_EA_INODE_FL);
	inode_test_stamp_raw_csum(ctx, 31, raw);
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_iget(ctx->sb, 31, EXT4_IGET_NORMAL)),
			-EFSCORRUPTED);
}

/* -------------------------------------------------------------------------
 * 10. Fast Symlink Classification & ext4_can_truncate() Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_fast_symlink_and_can_truncate(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;

	/* Fast symlink classification with/without external xattr & EA_INODE */
	inode_test_reset_mock_inode(ctx, S_IFLNK | 0777);
	inode->i_size = 20;
	inode->i_blocks = 0;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 1);

	/* External xattr block accounts for 8 512B sectors when !EA_INODE */
	ctx->ei.i_file_acl = INODE_TEST_META_BLK0;
	inode->i_blocks = 8;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 1);
	inode->i_blocks = 16;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 0);

	/* With EA_INODE feature, fast symlink depends on 0 < i_size < 60 */
	ext4_set_feature_ea_inode(ctx->sb);
	inode->i_size = 0;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 0);
	inode->i_size = 20;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 1);
	inode->i_size = EXT4_N_BLOCKS * 4;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 0);
	ext4_clear_feature_ea_inode(ctx->sb);

	/* Inline-data symlink is not classified as a legacy fast symlink */
	inode->i_size = 20;
	inode->i_blocks = 8;
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(inode), 0);

	/* ext4_can_truncate() across file types and fast vs slow symlinks */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 1);

	inode_test_reset_mock_inode(ctx, S_IFDIR | 0755);
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 1);

	inode_test_reset_mock_inode(ctx, S_IFIFO | 0600);
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 0);

	inode_test_reset_mock_inode(ctx, S_IFCHR | 0600);
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 0);

	inode_test_reset_mock_inode(ctx, S_IFLNK | 0777);
	inode->i_blocks = 0;
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 0);
	inode->i_blocks = 8;
	KUNIT_EXPECT_EQ(test, ext4_can_truncate(inode), 1);
}

/* -------------------------------------------------------------------------
 * 11. VFS Inode Flags, Address Space Ops, and Folio Order Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_flags_aops_mapping_order(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	const struct address_space_operations *da_aops, *nodelalloc_aops;
	const struct address_space_operations *jdata_aops;

	/* 1. ext4_set_inode_flags() flag translation & S_DAX policy */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	ctx->ei.i_flags = EXT4_SYNC_FL | EXT4_APPEND_FL | EXT4_IMMUTABLE_FL |
			  EXT4_NOATIME_FL | EXT4_DIRSYNC_FL | EXT4_VERITY_FL;
	ext4_set_inode_flags(inode, true);
	KUNIT_EXPECT_EQ(test,
			inode->i_flags & (S_SYNC | S_APPEND | S_IMMUTABLE |
					  S_NOATIME | S_DIRSYNC | S_VERITY),
			(unsigned int)(S_SYNC | S_APPEND | S_IMMUTABLE |
				       S_NOATIME | S_DIRSYNC | S_VERITY));

	ctx->ei.i_flags = 0;
	ext4_set_inode_flags(inode, false);
	KUNIT_EXPECT_EQ(test,
			inode->i_flags & (S_SYNC | S_APPEND | S_IMMUTABLE |
					  S_NOATIME | S_DIRSYNC), 0U);

	/* S_DAX enabled when BDEV_IS_DAX + DAX_ALWAYS on regular extent file */
	set_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);
	set_opt(ctx->sb, DAX_ALWAYS);
	ext4_set_inode_flags(inode, true);
	if (IS_ENABLED(CONFIG_FS_DAX))
		KUNIT_EXPECT_TRUE(test, IS_DAX(inode));

	/* DAX_NEVER and JOURNAL_DATA both suppress S_DAX */
	clear_opt(ctx->sb, DAX_ALWAYS);
	set_opt2(ctx->sb, DAX_NEVER);
	inode->i_flags &= ~S_DAX;
	ext4_set_inode_flags(inode, true);
	KUNIT_EXPECT_FALSE(test, IS_DAX(inode));
	clear_opt2(ctx->sb, DAX_NEVER);

	clear_opt(ctx->sb, DELALLOC);
	ctx->ei.i_flags = EXT4_DAX_FL | EXT4_JOURNAL_DATA_FL;
	ctx->sbi.s_journal = (journal_t *)1;
	ext4_set_inode_flags(inode, true);
	ctx->sbi.s_journal = NULL;
	set_opt(ctx->sb, DELALLOC);
	KUNIT_EXPECT_FALSE(test, IS_DAX(inode));
	clear_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);

	/* 2. ext4_set_aops() dispatch across delalloc, ordered, journalled */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	set_opt(ctx->sb, DELALLOC);
	ext4_set_aops(inode);
	da_aops = inode->i_mapping->a_ops;
	KUNIT_EXPECT_NOT_NULL(test, da_aops);

	clear_opt(ctx->sb, DELALLOC);
	ext4_set_aops(inode);
	nodelalloc_aops = inode->i_mapping->a_ops;
	KUNIT_EXPECT_PTR_NE(test, da_aops, nodelalloc_aops);

	ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	ctx->sbi.s_journal = (journal_t *)1;
	ext4_set_aops(inode);
	ctx->sbi.s_journal = NULL;
	jdata_aops = inode->i_mapping->a_ops;
	KUNIT_EXPECT_PTR_NE(test, jdata_aops, da_aops);
	KUNIT_EXPECT_PTR_NE(test, jdata_aops, nodelalloc_aops);
	set_opt(ctx->sb, DELALLOC);

	/* 3. ext4_set_inode_mapping_order() clamping */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	ctx->sbi.s_min_folio_order = 0;
	ctx->sbi.s_max_folio_order = 3;
	ext4_set_inode_mapping_order(inode);
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
		KUNIT_EXPECT_EQ(test,
				mapping_min_folio_order(inode->i_mapping), 0U);
		KUNIT_EXPECT_EQ(test,
				mapping_max_folio_order(inode->i_mapping), 3U);

		ctx->sbi.s_min_folio_order = 1;
		ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
		ext4_set_inode_mapping_order(inode);
		KUNIT_EXPECT_EQ(test,
				mapping_max_folio_order(inode->i_mapping), 1U);
	}
	ctx->sbi.s_min_folio_order = 0;
	ctx->sbi.s_max_folio_order = 0;
}

/* -------------------------------------------------------------------------
 * 12. Project ID, DIO Alignment, and getattr / file_getattr Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_projid_dio_and_getattr(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct path path = { .dentry = &ctx->root_dentry };
	struct kstat stat = {};
	kprojid_t projid = {};
	int err;

	/* ext4_get_projid(): -EOPNOTSUPP without feature, value with feature */
	ctx->ei.i_projid = make_kprojid(&init_user_ns, 314);
	ext4_clear_feature_project(ctx->sb);
	KUNIT_EXPECT_EQ(test, ext4_get_projid(inode, &projid), -EOPNOTSUPP);
	ext4_set_feature_project(ctx->sb);
	KUNIT_EXPECT_EQ(test, ext4_get_projid(inode, &projid), 0);
	KUNIT_EXPECT_EQ(test, from_kprojid(&init_user_ns, projid), 314U);

	/* ext4_dio_alignment(): 1 default, blocksize encrypted, 0 jdata/inline */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ext4_dio_alignment(inode), 1U);

	inode->i_flags |= S_ENCRYPTED;
	KUNIT_EXPECT_EQ(test, ext4_dio_alignment(inode),
			(u32)INODE_TEST_BLK_SIZE);
	inode->i_flags &= ~S_ENCRYPTED;

	clear_opt(ctx->sb, DELALLOC);
	ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	ctx->sbi.s_journal = (journal_t *)1;
	KUNIT_EXPECT_EQ(test, ext4_dio_alignment(inode), 0U);
	ctx->sbi.s_journal = NULL;
	ext4_clear_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	set_opt(ctx->sb, DELALLOC);

	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	KUNIT_EXPECT_EQ(test, ext4_dio_alignment(inode), 0U);
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ctx->ei.i_inline_off = 0;

	/* ext4_getattr() omits delalloc; ext4_file_getattr() includes it */
	inode->i_size = 8192;
	inode->i_blocks = 16;
	ctx->ei.i_reserved_data_blocks = 2;
	ctx->ei.i_crtime.tv_sec = 0x56781234LL;
	ctx->ei.i_crtime.tv_nsec = 999L;
	ctx->ei.i_flags |= EXT4_APPEND_FL | EXT4_IMMUTABLE_FL | EXT4_NODUMP_FL;
	ctx->sbi.s_awu_min = 4096;
	ctx->sbi.s_awu_max = 16384;

	err = ext4_getattr(&nop_mnt_idmap, &path, &stat,
			   STATX_BASIC_STATS | STATX_BTIME |
			   STATX_DIOALIGN | STATX_WRITE_ATOMIC, 0);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, stat.blocks, 16ULL);

	memset(&stat, 0, sizeof(stat));
	err = ext4_file_getattr(&nop_mnt_idmap, &path, &stat,
				STATX_BASIC_STATS | STATX_BTIME |
				STATX_DIOALIGN | STATX_WRITE_ATOMIC, 0);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, !!(stat.result_mask & STATX_BTIME));
	KUNIT_EXPECT_EQ(test, stat.btime.tv_sec, 0x56781234LL);
	KUNIT_EXPECT_EQ(test, stat.btime.tv_nsec, 999L);
	/* 16 on-disk sectors + 2 reserved 4K blocks (16 sectors) = 32 sectors */
	KUNIT_EXPECT_EQ(test, stat.blocks, 32ULL);
	KUNIT_EXPECT_EQ(test, stat.attributes & (STATX_ATTR_APPEND |
						 STATX_ATTR_IMMUTABLE |
						 STATX_ATTR_NODUMP),
			(u64)(STATX_ATTR_APPEND | STATX_ATTR_IMMUTABLE |
			      STATX_ATTR_NODUMP));
	KUNIT_EXPECT_EQ(test, stat.dio_mem_align, 512U);
	KUNIT_EXPECT_EQ(test, stat.dio_offset_align, 512U);
	KUNIT_EXPECT_EQ(test, stat.atomic_write_unit_min, 4096U);
	KUNIT_EXPECT_EQ(test, stat.atomic_write_unit_max, 16384U);

	/* Inline-data file reports simulated 512B sectors from i_size */
	ctx->ei.i_reserved_data_blocks = 0;
	inode->i_blocks = 0;
	inode->i_size = 1200;
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	memset(&stat, 0, sizeof(stat));
	err = ext4_file_getattr(&nop_mnt_idmap, &path, &stat,
				STATX_BASIC_STATS, 0);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, stat.blocks, 3ULL);
	ctx->sbi.s_awu_min = 0;
	ctx->sbi.s_awu_max = 0;
}

/* -------------------------------------------------------------------------
 * 13. Delayed Allocation Reservation, Claim, Release & Nonda Switch Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_da_reserve_and_release(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	int err;

	percpu_counter_set(&ctx->sbi.s_freeclusters_counter,
			   max_t(s64, 100, EXT4_FREECLUSTERS_WATERMARK + 100));
	percpu_counter_set(&ctx->sbi.s_dirtyclusters_counter, 0);

	/* Reserve 5 clusters */
	err = ext4_da_reserve_space_test(inode, 5);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 5U);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_dirtyclusters_counter),
			5LL);

	/* Claim 2 clusters via ext4_da_update_reserve_space() */
	ext4_da_update_reserve_space(inode, 2, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 3U);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_dirtyclusters_counter),
			3LL);

	/* Release 2 clusters via ext4_da_release_space() */
	ext4_da_release_space(inode, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 3U);
	ext4_da_release_space(inode, 2);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);

	/* Underflow in update_reserve_space clamps to 0 cleanly */
	kunit_warning_suppress(test) {
		ext4_da_update_reserve_space(inode, 4, 0);
	}
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 0U);

	/* Underflow in release_space clamps to 0 cleanly */
	err = ext4_da_reserve_space_test(inode, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	kunit_warning_suppress(test) {
		ext4_da_release_space(inode, 3);
	}
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 0U);

	/* Exhausted free clusters -> -ENOSPC & ext4_nonda_switch() */
	KUNIT_EXPECT_EQ(test, ext4_nonda_switch_test(ctx->sb), 0);
	percpu_counter_set(&ctx->sbi.s_freeclusters_counter, 2);
	percpu_counter_set(&ctx->sbi.s_dirtyclusters_counter, 4);
	KUNIT_EXPECT_EQ(test, ext4_nonda_switch_test(ctx->sb), 1);

	percpu_counter_set(&ctx->sbi.s_freeclusters_counter, 0);
	percpu_counter_set(&ctx->sbi.s_dirtyclusters_counter, 0);
	KUNIT_EXPECT_EQ(test, ext4_da_reserve_space_test(inode, 1), -ENOSPC);
}

/* -------------------------------------------------------------------------
 * 14. Bigalloc Delayed Allocation & Cluster Sharing Accounting Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_enable_bigalloc(struct inode_test_ctx *ctx,
				       unsigned int cluster_ratio)
{
	ext4_set_feature_bigalloc(ctx->sb);
	ctx->sbi.s_cluster_ratio = cluster_ratio;
	ctx->sbi.s_cluster_bits = ilog2(cluster_ratio);
	ctx->sbi.s_clusters_per_group =
		ctx->sbi.s_blocks_per_group / cluster_ratio;
}

static void inode_test_insert_leaf_extent(struct inode *inode, ext4_lblk_t lblk,
					  u16 len, ext4_fsblk_t pblk,
					  bool unwritten)
{
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	unsigned int idx = le16_to_cpu(eh->eh_entries);
	struct ext4_extent *ex = EXT_FIRST_EXTENT(eh) + idx;

	ex->ee_block = cpu_to_le32(lblk);
	ex->ee_len = cpu_to_le16(len);
	if (unwritten)
		ext4_ext_mark_unwritten(ex);
	ext4_ext_store_pblock(ex, pblk);
	eh->eh_entries = cpu_to_le16(idx + 1);
	ext4_es_remove_extent(inode, lblk, len);
}

static void inode_test_da_bigalloc_clusters(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_map_blocks map = {};
	struct buffer_head bh = {};
	int state_unalloc, state_delayed, state_mapped;
	int err;

	inode_test_enable_bigalloc(ctx, 4);
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	/* Pre-populate written extent at lblk 4 (lclu 1) -> pblk 104 */
	inode_test_insert_leaf_extent(inode, 4, 1, INODE_TEST_DATA_BLK0 + 4,
				      false);
	/* Pre-populate unwritten extent at lblk 8 (lclu 2) -> pblk 108 */
	inode_test_insert_leaf_extent(inode, 8, 2, 108, true);

	down_write(&ctx->ei.i_data_sem);
	state_unalloc = ext4_clu_alloc_state_test(inode, 0);
	state_mapped = ext4_clu_alloc_state_test(inode, 5);
	up_write(&ctx->ei.i_data_sem);
	KUNIT_EXPECT_EQ(test, state_unalloc, 0);
	KUNIT_EXPECT_EQ(test, state_mapped, 2);

	/* 1. First block in unallocated cluster 0 reserves 1 cluster */
	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 0, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_delay(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_new(&bh));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);

	down_write(&ctx->ei.i_data_sem);
	state_delayed = ext4_clu_alloc_state_test(inode, 2);
	up_write(&ctx->ei.i_data_sem);
	KUNIT_EXPECT_EQ(test, state_delayed, 1);

	/* 2. Sibling block in cluster 0 shares existing delalloc reservation */
	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 1, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_delay(&bh));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);

	/* Multi-block ext4_da_map_blocks() across [lblk 2..3] in cluster 0 */
	map = (struct ext4_map_blocks){ .m_lblk = 2, .m_len = 2 };
	err = ext4_da_map_blocks_test(inode, &map);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, map.m_len, 2U);
	KUNIT_EXPECT_TRUE(test, !!(map.m_flags & EXT4_MAP_DELAYED));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);

	map = (struct ext4_map_blocks){ .m_lblk = 0, .m_len = 4 };
	err = ext4_da_map_blocks_test(inode, &map);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, map.m_len, 4U);
	KUNIT_EXPECT_TRUE(test, !!(map.m_flags & EXT4_MAP_DELAYED));

	/*
	 * 3. Delayed block in cluster 1 (sharing written lblk 4) reserves 0 new
	 *    clusters and records a pending reservation on cluster 1!
	 */
	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 5, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_delay(&bh));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 4));

	/* 4. Already-written block 4 maps directly without BH_Delay */
	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 4, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_mapped(&bh));
	KUNIT_EXPECT_FALSE(test, buffer_delay(&bh));
	KUNIT_EXPECT_EQ(test, (ext4_fsblk_t)bh.b_blocknr,
			(ext4_fsblk_t)(INODE_TEST_DATA_BLK0 + 4));

	/*
	 * 5. Unwritten block 8 sets BH_Unwritten | BH_New | BH_Mapped;
	 *    delayed sibling hole block 10 in the same cluster 2 records a
	 *    pending reservation without reserving an extra cluster.
	 */
	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 8, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_unwritten(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_new(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_mapped(&bh));
	KUNIT_EXPECT_EQ(test, (ext4_fsblk_t)bh.b_blocknr, 108ULL);
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 8));

	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	err = ext4_da_get_block_prep(inode, 10, &bh, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_delay(&bh));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 8));

	/* Removing ES extents releases the 1 reserved cluster via da_release */
	down_write(&ctx->ei.i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	up_write(&ctx->ei.i_data_sem);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 0U);
}

/* -------------------------------------------------------------------------
 * 15. ext4_map_blocks() Lookup, Caching, Allocation, Zeroout & Validity Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_map_blocks_paths(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_map_blocks map = {};
	struct extent_status es = {};
	handle_t *handle;
	int ret;

	/* 1. Out-of-range logical block -> -EFSCORRUPTED */
	map.m_lblk = EXT_MAX_BLOCKS;
	map.m_len = 1;
	KUNIT_EXPECT_EQ(test, ext4_map_blocks(NULL, inode, &map, 0),
			-EFSCORRUPTED);

	/* 2. CACHED_NOWAIT miss returns 0 without querying extent tree */
	inode_test_insert_leaf_extent(inode, 0, 2, INODE_TEST_DATA_BLK0, false);
	map = (struct ext4_map_blocks){ .m_lblk = 0, .m_len = 2 };
	ret = ext4_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CACHED_NOWAIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* 3. ES miss queries extent tree, caches written extent; NOWAIT hits */
	map = (struct ext4_map_blocks){ .m_lblk = 0, .m_len = 2 };
	ret = ext4_map_blocks(NULL, inode, &map, 0);
	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, map.m_pblk, (ext4_fsblk_t)INODE_TEST_DATA_BLK0);
	KUNIT_EXPECT_TRUE(test, !!(map.m_flags & EXT4_MAP_MAPPED));

	map = (struct ext4_map_blocks){ .m_lblk = 1, .m_len = 1 };
	ret = ext4_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CACHED_NOWAIT);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, map.m_pblk,
			(ext4_fsblk_t)(INODE_TEST_DATA_BLK0 + 1));

	/* 4. ES delayed & hole hits */
	down_write(&ctx->ei.i_data_sem);
	KUNIT_EXPECT_EQ(test, ext4_insert_delayed_blocks_test(inode, 4, 2), 0);
	ext4_es_insert_extent(inode, 6, 2, ~0, EXTENT_STATUS_HOLE, 0);
	up_write(&ctx->ei.i_data_sem);

	map = (struct ext4_map_blocks){ .m_lblk = 4, .m_len = 2 };
	ret = ext4_map_blocks(NULL, inode, &map, 0);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, !!(map.m_flags & EXT4_MAP_DELAYED));

	map = (struct ext4_map_blocks){ .m_lblk = 6, .m_len = 2 };
	ret = ext4_map_blocks(NULL, inode, &map, 0);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_flags, 0U);

	/* 5. Block allocation + EXT4_GET_BLOCKS_CREATE_ZERO & zeroout error */
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0 + 2;
	ctx->zeroout_calls = 0;
	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 4);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	map = (struct ext4_map_blocks){ .m_lblk = 10, .m_len = 2 };
	ret = ext4_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE_ZERO);
	ext4_journal_stop(handle);

	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, map.m_pblk,
			(ext4_fsblk_t)(INODE_TEST_DATA_BLK0 + 2));
	KUNIT_EXPECT_TRUE(test, !!(map.m_flags & EXT4_MAP_NEW));
	KUNIT_EXPECT_EQ(test, ctx->zeroout_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_lblk, 10U);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_len, 2U);

	/* Failed zeroout returns error BEFORE caching written extent in ES */
	ctx->fail_zeroout_err = -EIO;
	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 4);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	map = (struct ext4_map_blocks){ .m_lblk = 12, .m_len = 1 };
	ret = ext4_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE_ZERO);
	ext4_journal_stop(handle);
	ctx->fail_zeroout_err = 0;

	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 12, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_FALSE(test, ext4_es_is_written(&es));
	KUNIT_EXPECT_TRUE(test, ext4_es_is_hole(&es));

	/* 6. check_block_validity() rejects out-of-bounds pblk in extent */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	inode_test_insert_leaf_extent(inode, 0, 1, INODE_TEST_TOTAL_BLKS + 50,
				      false);
	map = (struct ext4_map_blocks){ .m_lblk = 0, .m_len = 1 };
	KUNIT_EXPECT_EQ(test, ext4_map_blocks(NULL, inode, &map, 0),
			-EFSCORRUPTED);
}

/* -------------------------------------------------------------------------
 * 16. ext4_get_block, getblk, bread, bread_batch & walk_page_buffers Tests
 * -------------------------------------------------------------------------
 */

static int inode_test_walk_cb(handle_t *handle, struct inode *inode,
			      struct buffer_head *bh)
{
	int *visited = (int *)bh->b_private;

	(*visited)++;
	if (*visited < 0)
		return -EINVAL;
	return 0;
}

static void inode_test_get_block_bread_and_walk_buffers(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *dblk0 = inode_test_find_blk(ctx,
							   INODE_TEST_DATA_BLK0);
	struct inode_test_blk *dblk1 = inode_test_find_blk(ctx,
							   INODE_TEST_DATA_BLK0 + 1);
	struct buffer_head bh = {};
	struct buffer_head *res_bh;
	struct buffer_head *batch[3] = {};
	struct buffer_head ring[4] = {};
	int visited[4] = { 0, 0, 0, 0 };
	int partial = 0;
	handle_t *handle;
	int i, err;

	/* 1. ext4_get_block() inline -ERANGE, hole lookup, mapped & create */
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	bh.b_size = INODE_TEST_BLK_SIZE;
	KUNIT_EXPECT_EQ(test, ext4_get_block(inode, 0, &bh, 0), -ERANGE);
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	ctx->ei.i_inline_off = 0;

	memset(&bh, 0, sizeof(bh));
	bh.b_size = INODE_TEST_BLK_SIZE;
	KUNIT_EXPECT_EQ(test, ext4_get_block(inode, 0, &bh, 0), 0);
	KUNIT_EXPECT_FALSE(test, buffer_mapped(&bh));

	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0;
	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 4);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	err = ext4_get_block_unwritten(inode, 0, &bh, 1);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, buffer_mapped(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_unwritten(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_new(&bh));

	/* 2. ext4_getblk() hole -> NULL, CREATE new block zeroes & dirties bh */
	res_bh = ext4_getblk(NULL, inode, 5, 0);
	KUNIT_EXPECT_NULL(test, res_bh);

	clear_buffer_uptodate(&dblk1->bh);
	memset(dblk1->cache_buf, 0xcd, INODE_TEST_BLK_SIZE);
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0 + 1;
	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 4);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	res_bh = ext4_getblk(handle, inode, 1, EXT4_GET_BLOCKS_CREATE);
	ext4_journal_stop(handle);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(res_bh));
	KUNIT_EXPECT_TRUE(test, buffer_uptodate(res_bh));
	KUNIT_EXPECT_EQ(test, (u8)dblk1->cache_buf[0], 0);
	brelse(res_bh);

	/* 3. ext4_bread() disk read & -EIO error cleanup */
	inode_test_reset_mock_inode(ctx, S_IFDIR | 0755);
	inode_test_insert_leaf_extent(inode, 0, 1, INODE_TEST_DATA_BLK0, false);
	inode_test_insert_leaf_extent(inode, 2, 1, INODE_TEST_DATA_BLK0 + 1,
				      false);
	clear_buffer_uptodate(&dblk0->bh);
	memset(dblk0->disk_buf, 0x3c, INODE_TEST_BLK_SIZE);
	ctx->read_lock_calls = 0;

	res_bh = ext4_bread(NULL, inode, 0, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(res_bh));
	KUNIT_EXPECT_EQ(test, ctx->read_lock_calls, 1U);
	KUNIT_EXPECT_EQ(test, (u8)res_bh->b_data[0], 0x3c);
	brelse(res_bh);

	clear_buffer_uptodate(&dblk0->bh);
	ctx->fail_read_eio = true;
	res_bh = ext4_bread(NULL, inode, 0, 0);
	ctx->fail_read_eio = false;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res_bh), -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&dblk0->bh.b_count), 1);

	/* 4. ext4_bread_batch() across [mapped, hole, mapped] & -EIO cleanup */
	clear_buffer_uptodate(&dblk0->bh);
	clear_buffer_uptodate(&dblk1->bh);
	memset(dblk1->disk_buf, 0x99, INODE_TEST_BLK_SIZE);
	err = ext4_bread_batch(inode, 0, 3, true, batch);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_NOT_NULL(test, batch[0]);
	KUNIT_EXPECT_NULL(test, batch[1]);
	KUNIT_EXPECT_NOT_NULL(test, batch[2]);
	KUNIT_EXPECT_EQ(test, (u8)batch[2]->b_data[0], 0x99);
	for (i = 0; i < 3; i++)
		brelse(batch[i]);

	memset(batch, 0, sizeof(batch));
	clear_buffer_uptodate(&dblk0->bh);
	ctx->fail_read_eio = true;
	err = ext4_bread_batch(inode, 0, 3, true, batch);
	ctx->fail_read_eio = false;
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&dblk0->bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&dblk1->bh.b_count), 1);

	/* 5. ext4_walk_page_buffers() sub-page range, partial & error stop */
	for (i = 0; i < 4; i++) {
		ring[i].b_size = 1024;
		ring[i].b_this_page = &ring[(i + 1) % 4];
		ring[i].b_private = &visited[i];
		if (i != 3)
			set_buffer_uptodate(&ring[i]);
	}
	err = ext4_walk_page_buffers(NULL, inode, &ring[0], 1024, 3072,
				     &partial, inode_test_walk_cb);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, visited[0], 0);
	KUNIT_EXPECT_EQ(test, visited[1], 1);
	KUNIT_EXPECT_EQ(test, visited[2], 1);
	KUNIT_EXPECT_EQ(test, visited[3], 0);
	KUNIT_EXPECT_EQ(test, partial, 1);

	set_buffer_uptodate(&ring[3]);
	partial = 0;
	err = ext4_walk_page_buffers(NULL, inode, &ring[0], 1024, 3072,
				     &partial, inode_test_walk_cb);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, partial, 0);

	visited[1] = -2;
	err = ext4_walk_page_buffers(NULL, inode, &ring[0], 1024, 3072,
				     NULL, inode_test_walk_cb);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);
	KUNIT_EXPECT_EQ(test, visited[2], 2);
}

/* -------------------------------------------------------------------------
 * 17. ext4_iomap_begin, ext4_iomap_report_ops, and Atomic Writes Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_iomap_and_atomic_writes(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct iomap iomap = {};
	struct iomap srcmap = {};
	struct iomap_iter iter = {};
	int err;

	/* 1. Offset past EXT4_MAX_LOGICAL_BLOCK -> -EINVAL */
	err = ext4_iomap_begin(inode,
			       ((loff_t)EXT4_MAX_LOGICAL_BLOCK + 1) << 12,
			       4096, 0, &iomap, &srcmap);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* 2. Inline-data: iomap_begin warns & returns -ERANGE; report works */
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_inline_off = 164;
	ctx->ei.i_inline_size = 60;
	inode->i_size = 24;

	kunit_warning_suppress(test) {
		err = ext4_iomap_begin(inode, 0, 24, 0, &iomap, &srcmap);
	}
	KUNIT_EXPECT_EQ(test, err, -ERANGE);

	iter = (struct iomap_iter){
		.inode = inode, .pos = 0, .len = 24, .flags = IOMAP_REPORT,
	};
	err = ext4_iomap_next_report(&iter, &iter.iomap, &iter.srcmap);
	KUNIT_EXPECT_GT(test, err, 0);
	KUNIT_EXPECT_EQ(test, iter.iomap.type, (u16)IOMAP_INLINE);

	iter = (struct iomap_iter){
		.inode = inode, .pos = 60, .len = 24, .flags = IOMAP_REPORT,
	};
	err = ext4_iomap_next_report(&iter, &iter.iomap, &iter.srcmap);
	KUNIT_EXPECT_EQ(test, err, -ENOENT);

	/* 3. Indirect report past s_bitmap_maxbytes returns IOMAP_HOLE */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	memset(ctx->ei.i_data, 0, sizeof(ctx->ei.i_data));
	iter = (struct iomap_iter){
		.inode = inode,
		.pos = ctx->sbi.s_bitmap_maxbytes,
		.len = 4096,
		.flags = IOMAP_REPORT,
	};
	err = ext4_iomap_next_report(&iter, &iter.iomap, &iter.srcmap);
	KUNIT_EXPECT_GT(test, err, 0);
	KUNIT_EXPECT_EQ(test, iter.iomap.type, (u16)IOMAP_HOLE);

	/* Indirect DIO write to hole inside i_size returns -ENOTBLK */
	inode->i_size = 8192;
	err = ext4_iomap_begin(inode, 0, 4096, IOMAP_WRITE | IOMAP_DIRECT,
			       &iomap, &srcmap);
	KUNIT_EXPECT_EQ(test, err, -ENOTBLK);

	/* 4. Read mappings (MAPPED, UNWRITTEN, DELALLOC, HOLE, DAX offset) */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	inode->i_size = 16384;
	ctx->sbi.s_dax_part_off = 0x200000ULL;
	down_write(&ctx->ei.i_data_sem);
	ext4_es_insert_extent(inode, 0, 1, INODE_TEST_DATA_BLK0,
			      EXTENT_STATUS_WRITTEN, 0);
	ext4_es_insert_extent(inode, 1, 1, INODE_TEST_DATA_BLK0 + 1,
			      EXTENT_STATUS_UNWRITTEN, 0);
	KUNIT_EXPECT_EQ(test, ext4_insert_delayed_blocks_test(inode, 2, 1), 0);
	ext4_es_insert_extent(inode, 3, 1, ~0, EXTENT_STATUS_HOLE, 0);
	up_write(&ctx->ei.i_data_sem);

	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 0, 4096, IOMAP_DAX, &iomap,
					 &srcmap), 0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_MAPPED);
	KUNIT_EXPECT_EQ(test, iomap.addr,
			((u64)INODE_TEST_DATA_BLK0 << 12) +
			(IS_ENABLED(CONFIG_FS_DAX) ? 0x200000ULL : 0ULL));
	ctx->sbi.s_dax_part_off = 0;

	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 4096, 4096, 0, &iomap, &srcmap),
			0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_UNWRITTEN);

	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 8192, 4096, 0, &iomap, &srcmap),
			0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_DELALLOC);

	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 12288, 4096, 0, &iomap,
					 &srcmap), 0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_HOLE);

	iter = (struct iomap_iter){
		.inode = inode, .pos = 0, .len = 4096, .flags = 0,
	};
	err = ext4_iomap_next(&iter, &iter.iomap, &iter.srcmap);
	KUNIT_EXPECT_GT(test, err, 0);
	KUNIT_EXPECT_EQ(test, iter.iomap.type, (u16)IOMAP_MAPPED);
	KUNIT_EXPECT_EQ(test, iter.iomap.addr,
			(u64)INODE_TEST_DATA_BLK0 << 12);

	/* 5. DIO write: NOWAIT -> -EAGAIN, inside i_size -> UNWRITTEN */
	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 12288, 4096,
					 IOMAP_WRITE | IOMAP_DIRECT |
					 IOMAP_NOWAIT, &iomap, &srcmap),
			-EAGAIN);

	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0 + 3;
	KUNIT_EXPECT_EQ(test,
			ext4_iomap_begin(inode, 12288, 4096,
					 IOMAP_WRITE | IOMAP_DIRECT, &iomap,
					 &srcmap), 0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_UNWRITTEN);
	KUNIT_EXPECT_TRUE(test, !!(iomap.flags & IOMAP_F_NEW));

	/* 6. Bigalloc multi-block atomic write across mixed [written + hole] */
	inode_test_enable_bigalloc(ctx, 4);
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	inode->i_size = 16384;
	/* Pre-populate lblk 0..1 as written at pblk 100..101; lblk 2..3 hole */
	inode_test_insert_leaf_extent(inode, 0, 2, INODE_TEST_DATA_BLK0, false);
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0 + 2;
	ctx->zeroout_calls = 0;

	err = ext4_iomap_begin(inode, 0, 16384,
			       IOMAP_WRITE | IOMAP_DIRECT | IOMAP_ATOMIC,
			       &iomap, &srcmap);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_MAPPED);
	KUNIT_EXPECT_EQ(test, iomap.length, 16384ULL);
	KUNIT_EXPECT_EQ(test, iomap.addr, (u64)INODE_TEST_DATA_BLK0 << 12);
	KUNIT_EXPECT_TRUE(test, !!(iomap.flags & IOMAP_F_ATOMIC_BIO));
	KUNIT_EXPECT_GT(test, ctx->zeroout_calls, 0U);
}

/* -------------------------------------------------------------------------
 * 18. Transaction Credits, Layout Breaking & Disksize Update Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_trans_credits_and_disksize(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	int ext_credits, ind_credits, chunk_ext, chunk_jdata;
	int err;

	/* 1. ext4_meta_trans_blocks() & ext4_chunk_trans_*() */
	ctx->sbi.s_groups_count = 4;
	ctx->sbi.s_gdb_count = 2;
	ext_credits = ext4_meta_trans_blocks(inode, 8, 2, 1);
	KUNIT_EXPECT_GT(test, ext_credits, 0);

	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ind_credits = ext4_meta_trans_blocks(inode, 8, 2, 1);
	KUNIT_EXPECT_GT(test, ind_credits, 0);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	ctx->sbi.s_groups_count = 1;
	ctx->sbi.s_gdb_count = 1;

	chunk_ext = ext4_chunk_trans_extent(inode, 5);
	KUNIT_EXPECT_EQ(test, ext4_chunk_trans_blocks(inode, 5), chunk_ext);

	clear_opt(ctx->sb, DELALLOC);
	ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	ctx->sbi.s_journal = (journal_t *)1;
	chunk_jdata = ext4_chunk_trans_extent(inode, 5);
	ctx->sbi.s_journal = NULL;
	ext4_clear_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	set_opt(ctx->sb, DELALLOC);
	KUNIT_EXPECT_EQ(test, chunk_jdata, chunk_ext + 5);

	/* 2. ext4_break_layouts() requires invalidate_lock held */
	kunit_warning_suppress(test) {
		err = ext4_break_layouts(inode);
	}
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	filemap_invalidate_lock(inode->i_mapping);
	err = ext4_break_layouts(inode);
	filemap_invalidate_unlock(inode->i_mapping);
	KUNIT_EXPECT_EQ(test, err, 0);

	/* 3. ext4_update_disksize_before_punch() */
	inode->i_size = 12000;
	ctx->ei.i_disksize = 4096;

	inode_lock(inode);
	/* Punch past i_size is a no-op */
	KUNIT_EXPECT_EQ(test,
			ext4_update_disksize_before_punch(inode, 16384, 4096),
			0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 4096LL);

	/* Punch within [0, 8192) advances i_disksize to 8192 */
	KUNIT_EXPECT_EQ(test,
			ext4_update_disksize_before_punch(inode, 4096, 4096),
			0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 8192LL);

	/* Punch extending past i_size clamps i_disksize to i_size (12000) */
	KUNIT_EXPECT_EQ(test,
			ext4_update_disksize_before_punch(inode, 8192, 8192),
			0);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 12000LL);
}

/* -------------------------------------------------------------------------
 * 19. Partial Block Zeroing, EOF Zeroing & Pagecache Truncate Range Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_zero_partial_and_eof(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *dblk0 = inode_test_find_blk(ctx,
							   INODE_TEST_DATA_BLK0);
	struct inode_test_blk *dblk1 = inode_test_find_blk(ctx,
							   INODE_TEST_DATA_BLK0 + 1);
	struct folio *folio;
	unsigned int partial_zeroed = 0;
	u8 *kaddr;
	int err;

	/*
	 * Map lblk 0 -> pblk 100 (written), leave lblk 1 as a hole, and map
	 * lblk 2 -> pblk 101 (written).
	 */
	inode->i_size = 12288;
	ctx->ei.i_disksize = 12288;
	inode_test_insert_leaf_extent(inode, 0, 1, INODE_TEST_DATA_BLK0, false);
	inode_test_insert_leaf_extent(inode, 2, 1, INODE_TEST_DATA_BLK0 + 1,
				      false);
	memset(dblk0->disk_buf, 0x5a, INODE_TEST_BLK_SIZE);
	memset(dblk1->disk_buf, 0xa5, INODE_TEST_BLK_SIZE);

	/* 1. Single-block interior zeroing [512, 1536) sets START | END */
	err = ext4_zero_partial_blocks(inode, 512, 1024, &partial_zeroed);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, partial_zeroed,
			(unsigned int)(EXT4_PARTIAL_ZERO_START |
				       EXT4_PARTIAL_ZERO_END));

	folio = filemap_get_folio(inode->i_mapping, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	kaddr = kmap_local_folio(folio, 0);
	KUNIT_EXPECT_EQ(test, kaddr[511], 0x5a);
	KUNIT_EXPECT_EQ(test, kaddr[512], 0x00);
	KUNIT_EXPECT_EQ(test, kaddr[1535], 0x00);
	KUNIT_EXPECT_EQ(test, kaddr[1536], 0x5a);
	kunmap_local(kaddr);
	folio_put(folio);

	/* 2. Multi-block [3000, 5000): lblk 0 mapped, lblk 1 hole -> START */
	partial_zeroed = 0;
	err = ext4_zero_partial_blocks(inode, 3000, 2000, &partial_zeroed);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, partial_zeroed,
			(unsigned int)EXT4_PARTIAL_ZERO_START);

	/* Zero EOF tail [8500, 12288) in lblk 2 -> pblk 101 */
	KUNIT_EXPECT_EQ(test, ext4_block_zero_eof(inode, 8192, 12288), 0);
	KUNIT_EXPECT_EQ(test, ext4_block_zero_eof(inode, 9000, 8500), 0);

	err = ext4_block_zero_eof(inode, 8500, 12288);
	KUNIT_ASSERT_EQ(test, err, 0);

	folio = filemap_get_folio(inode->i_mapping, 8192 >> PAGE_SHIFT);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	kaddr = kmap_local_folio(folio, offset_in_folio(folio, 8192));
	KUNIT_EXPECT_EQ(test, kaddr[307], 0xa5);
	KUNIT_EXPECT_EQ(test, kaddr[308], 0x00);
	KUNIT_EXPECT_EQ(test, kaddr[4095], 0x00);
	kunmap_local(kaddr);
	folio_put(folio);

	/* 3. ext4_truncate_page_cache_block_range() evicts full folio 0 */
	err = ext4_truncate_page_cache_block_range(inode, 0,
						   INODE_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, err, 0);
	err = ext4_truncate_page_cache_block_range(inode, 0,
				max_t(loff_t, PAGE_SIZE, INODE_TEST_BLK_SIZE));
	KUNIT_EXPECT_EQ(test, err, 0);
	folio = filemap_get_folio(inode->i_mapping, 0);
	KUNIT_EXPECT_TRUE(test, IS_ERR(folio));
	if (!IS_ERR(folio))
		folio_put(folio);
}

/* -------------------------------------------------------------------------
 * 20. ext4_punch_hole() & ext4_truncate() End-to-End Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_punch_hole_and_truncate(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct file *dummy_file;
	struct extent_status es = {};
	int err;

	dummy_file = kunit_kzalloc(test, sizeof(*dummy_file), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dummy_file);
	dummy_file->f_inode = inode;
	dummy_file->f_mapping = inode->i_mapping;

	/* Set up 4-block written extent [lblk 0..3 -> pblk 100..103] */
	inode->i_size = 4 * INODE_TEST_BLK_SIZE;
	ctx->ei.i_disksize = inode->i_size;
	inode->i_blocks = 32;
	inode_test_insert_leaf_extent(inode, 0, 4, INODE_TEST_DATA_BLK0, false);
	down_write(&ctx->ei.i_data_sem);
	ext4_es_insert_extent(inode, 0, 4, INODE_TEST_DATA_BLK0,
			      EXTENT_STATUS_WRITTEN, 0);
	up_write(&ctx->ei.i_data_sem);

	/* 1. Punch past i_size is a no-op */
	inode_lock(inode);
	err = ext4_punch_hole(dummy_file, 5 * INODE_TEST_BLK_SIZE,
			      INODE_TEST_BLK_SIZE);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 0UL);

	/* 2. Punch middle 2 blocks [4096, 12288) -> frees pblk 101..102 */
	inode_lock(inode);
	err = ext4_punch_hole(dummy_file, INODE_TEST_BLK_SIZE,
			      2 * INODE_TEST_BLK_SIZE);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 2UL);
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 1, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_hole(&es));
	KUNIT_EXPECT_EQ(test, es.es_len, 2U);

	/* 3. Truncate down to 1 block (4096B) -> frees trailing pblk 103 */
	ctx->total_freed_blocks = 0;
	ctx->orphan_add_calls = 0;
	ctx->orphan_del_calls = 0;
	inode->i_size = INODE_TEST_BLK_SIZE;
	inode_lock(inode);
	err = ext4_truncate(inode);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->orphan_add_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->orphan_del_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 1UL);
	KUNIT_EXPECT_EQ(test, ctx->last_free_pblk,
			(ext4_fsblk_t)(INODE_TEST_DATA_BLK0 + 3));

	/* 4. Truncate to 0 sets EXT4_STATE_DA_ALLOC_CLOSE & frees pblk 100 */
	ctx->total_freed_blocks = 0;
	inode->i_size = 0;
	inode_lock(inode);
	err = ext4_truncate(inode);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(inode,
						EXT4_STATE_DA_ALLOC_CLOSE));
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 1UL);
	KUNIT_EXPECT_EQ(test, ctx->last_free_pblk,
			(ext4_fsblk_t)INODE_TEST_DATA_BLK0);
}

/* -------------------------------------------------------------------------
 * 21. ext4_inode_attach_jinode, ext4_write_inode & ext4_sync_inode_metadata
 * -------------------------------------------------------------------------
 */

static void inode_test_write_and_sync_metadata(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *itable = inode_test_find_blk(ctx,
							   INODE_TEST_ITABLE_BLK0);
	struct writeback_control wbc = { .sync_mode = WB_SYNC_ALL };
	struct ext4_inode *raw;
	int err;

	/* 1. ext4_inode_attach_jinode(): no-op without journal, allocs with */
	KUNIT_EXPECT_EQ(test, ext4_inode_attach_jinode(inode), 0);
	KUNIT_EXPECT_NULL(test, ctx->ei.jinode);

	ctx->sbi.s_journal = (journal_t *)1;
	err = ext4_inode_attach_jinode(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_NOT_NULL(test, ctx->ei.jinode);
	/* Idempotent when jinode is already attached */
	KUNIT_EXPECT_EQ(test, ext4_inode_attach_jinode(inode), 0);
	ctx->sbi.s_journal = NULL;
	if (ctx->ei.jinode) {
		jbd2_free_inode(ctx->ei.jinode);
		ctx->ei.jinode = NULL;
	}

	/* 2. ext4_write_inode(): 0 in nojournal, -EROFS in emergency state */
	KUNIT_EXPECT_EQ(test, ext4_write_inode(inode, &wbc), 0);
	set_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, ext4_write_inode(inode, &wbc), -EROFS);
	clear_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);

	/* 3. ext4_sync_inode_metadata(): warns & fails if journal present */
	ctx->sbi.s_journal = (journal_t *)1;
	kunit_warning_suppress(test) {
		err = ext4_sync_inode_metadata(inode, &wbc);
	}
	ctx->sbi.s_journal = NULL;
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);

	/* Clean nojournal metadata sync flushes dirty raw inode & returns 0 */
	i_uid_write(inode, 0x7788);
	ext4_dirty_inode(inode, I_DIRTY_SYNC);
	clear_buffer_mapped(&itable->bh);
	clear_buffer_write_io_error(&itable->bh);
	err = ext4_sync_inode_metadata(inode, &wbc);
	KUNIT_EXPECT_EQ(test, err, 0);
	raw = inode_test_raw_slot(ctx, INODE_TEST_PRIMARY_INO, false);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(raw->i_uid_low), 0x7788);

	/* Simulated buffer write error returns -EIO */
	set_buffer_write_io_error(&itable->bh);
	err = ext4_sync_inode_metadata(inode, &wbc);
	clear_buffer_write_io_error(&itable->bh);
	KUNIT_EXPECT_EQ(test, err, -EIO);
}

/* -------------------------------------------------------------------------
 * 22. ext4_evict_inode() Live, Unlinked Regular & Fast Symlink Eviction Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_evict_inode_paths(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct ext4_inode *raw;
	struct inode *ig;

	/* 1. Live inode (i_nlink == 1): evicts cleanly without freeing inode */
	inode_test_prepare_valid_raw_inode(ctx, 24, S_IFREG | 0644);
	raw = inode_test_raw_slot(ctx, 24, true);
	raw->i_size_lo = cpu_to_le32(4096);
	inode_test_stamp_raw_csum(ctx, 24, raw);
	ig = ext4_iget(ctx->sb, 24, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ig));
	ctx->free_inode_calls = 0;
	ctx->total_freed_blocks = 0;
	iput(ig);
	KUNIT_EXPECT_EQ(test, ctx->free_inode_calls, 0U);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 0UL);

	/*
	 * 2. Unlinked regular file (i_nlink == 0) with 2 mapped blocks:
	 *    truncates data blocks, sets i_dtime, updates raw inode, and calls
	 *    ext4_free_inode().
	 */
	{
		u32 t_before, t_after, dtime;

		inode_test_prepare_valid_raw_inode(ctx, 25, S_IFREG | 0644);
		raw = inode_test_raw_slot(ctx, 25, true);
		raw->i_size_lo = cpu_to_le32(8192);
		inode_test_stamp_raw_csum(ctx, 25, raw);
		ig = ext4_iget(ctx->sb, 25, EXT4_IGET_NORMAL);
		KUNIT_ASSERT_FALSE(test, IS_ERR(ig));
		inode_test_insert_leaf_extent(ig, 0, 2, INODE_TEST_DATA_BLK0,
					      false);
		ig->i_blocks = 16;
		clear_nlink(ig);
		ctx->free_inode_calls = 0;
		ctx->total_freed_blocks = 0;
		raw = inode_test_raw_slot(ctx, 25, false);
		raw->i_dtime = cpu_to_le32(0xdeadbeefU);
		inode_test_stamp_raw_csum(ctx, 25, raw);
		t_before = (__u32)ktime_get_real_seconds();
		iput(ig);
		t_after = (__u32)ktime_get_real_seconds();
		KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 2UL);
		KUNIT_EXPECT_EQ(test, ctx->free_inode_calls, 1U);
		raw = inode_test_raw_slot(ctx, 25, false);
		dtime = le32_to_cpu(raw->i_dtime);
		KUNIT_EXPECT_LE(test, (u32)(dtime - t_before),
				(u32)(t_after - t_before));
		KUNIT_EXPECT_EQ(test, le32_to_cpu(raw->i_size_lo), 0U);
	}

	/*
	 * 3. Unlinked fast symlink (i_nlink == 0): clears inline i_data target
	 *    without treating bytes as block pointers and frees the inode.
	 */
	inode_test_prepare_valid_raw_inode(ctx, 26, S_IFLNK | 0777);
	raw = inode_test_raw_slot(ctx, 26, true);
	raw->i_flags = 0;
	raw->i_size_lo = cpu_to_le32(11);
	memcpy(raw->i_block, "target/path", 11);
	inode_test_stamp_raw_csum(ctx, 26, raw);
	ig = ext4_iget(ctx->sb, 26, EXT4_IGET_NORMAL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ig));
	KUNIT_EXPECT_EQ(test, ext4_inode_is_fast_symlink(ig), 1);
	clear_nlink(ig);
	ctx->free_inode_calls = 0;
	ctx->total_freed_blocks = 0;
	iput(ig);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 0UL);
	KUNIT_EXPECT_EQ(test, ctx->free_inode_calls, 1U);
}

/* -------------------------------------------------------------------------
 * 23. Buffered Write (ext4_da_aops & ext4_aops) and Folio Operations Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_buffered_write_and_folio_ops(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio = NULL;
	void *fsdata = NULL;
	u8 *kaddr;
	int ret;

	/*
	 * 1. Delayed-allocation buffered write to hole [0, 128):
	 *    reserves 1 delalloc block and updates i_size while deferring
	 *    i_disksize update until writeback.
	 */
	inode_lock(inode);
	ret = mapping->a_ops->write_begin(NULL, mapping, 0, 128, &folio,
					  &fsdata);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0xab, 128);
	kunmap_local(kaddr);
	ret = mapping->a_ops->write_end(NULL, mapping, 0, 128, 128, folio,
					fsdata);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 128);
	KUNIT_EXPECT_EQ(test, inode->i_size, 128LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 0LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);

	/*
	 * 2. Delayed-allocation write into an already-written block past i_size
	 *    (placed on folio index 1 regardless of PAGE_SIZE) updates both
	 *    i_size and i_disksize immediately.
	 */
	{
		loff_t wpos = max_t(loff_t, PAGE_SIZE, INODE_TEST_BLK_SIZE);
		ext4_lblk_t wlblk = wpos >> INODE_TEST_BLK_BITS;

		inode_test_insert_leaf_extent(inode, wlblk, 1,
					      INODE_TEST_DATA_BLK0 + 1, false);
		inode_lock(inode);
		ret = mapping->a_ops->write_begin(NULL, mapping, wpos, 64,
						  &folio, &fsdata);
		KUNIT_ASSERT_EQ(test, ret, 0);
		kaddr = kmap_local_folio(folio, offset_in_folio(folio, wpos));
		memset(kaddr, 0xcd, 64);
		kunmap_local(kaddr);
		ret = mapping->a_ops->write_end(NULL, mapping, wpos, 64, 64,
						folio, fsdata);
		inode_unlock(inode);
		KUNIT_EXPECT_EQ(test, ret, 64);
		KUNIT_EXPECT_EQ(test, inode->i_size, wpos + 64);
		KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, wpos + 64);

		/* Full folio invalidation detaches buffers cleanly */
		folio = filemap_lock_folio(mapping, wpos >> PAGE_SHIFT);
		KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
		KUNIT_EXPECT_NOT_NULL(test, folio_buffers(folio));
		mapping->a_ops->invalidate_folio(folio, 0, folio_size(folio));
		KUNIT_EXPECT_NULL(test, folio_buffers(folio));
		folio_unlock(folio);
		folio_put(folio);
	}

	/* 3. Non-delalloc buffered write (ext4_aops) allocates pblk directly */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	clear_opt(ctx->sb, DELALLOC);
	ext4_set_aops(inode);
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0;
	ctx->alloc_calls = 0;

	inode_lock(inode);
	ret = mapping->a_ops->write_begin(NULL, mapping, 0, 256, &folio,
					  &fsdata);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0xef, 256);
	kunmap_local(kaddr);
	ret = mapping->a_ops->write_end(NULL, mapping, 0, 256, 256, folio,
					fsdata);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 256);
	KUNIT_EXPECT_EQ(test, ctx->alloc_calls, 1U);
	KUNIT_EXPECT_EQ(test, inode->i_size, 256LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 256LL);

	/* 4. a_ops->bmap, partial invalidate_folio & release_folio */
	KUNIT_EXPECT_EQ(test, (ext4_fsblk_t)mapping->a_ops->bmap(mapping, 0),
			(ext4_fsblk_t)INODE_TEST_DATA_BLK0);
	KUNIT_EXPECT_EQ(test, (ext4_fsblk_t)mapping->a_ops->bmap(mapping, 5),
			0ULL);

	folio = filemap_lock_folio(mapping, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	folio_set_checked(folio);
	KUNIT_EXPECT_FALSE(test, mapping->a_ops->release_folio(folio,
							       GFP_KERNEL));
	folio_clear_checked(folio);
	/* Dirty buffer still pins folio buffers */
	KUNIT_EXPECT_FALSE(test, mapping->a_ops->release_folio(folio,
							       GFP_KERNEL));
	/* Partial invalidation preserves buffer ring */
	mapping->a_ops->invalidate_folio(folio, 128, 128);
	KUNIT_EXPECT_NOT_NULL(test, folio_buffers(folio));

	clear_buffer_dirty(folio_buffers(folio));
	folio_clear_dirty_for_io(folio);
	KUNIT_EXPECT_TRUE(test, mapping->a_ops->release_folio(folio,
							      GFP_KERNEL));
	KUNIT_EXPECT_NULL(test, folio_buffers(folio));
	folio_unlock(folio);
	folio_put(folio);
	set_opt(ctx->sb, DELALLOC);
}

/* -------------------------------------------------------------------------
 * 24. ext4_setattr() Permission, Mode, Extend & Shrink Truncate Tests
 * -------------------------------------------------------------------------
 */

static void inode_test_setattr(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct inode_test_blk *dblk0 = inode_test_find_blk(ctx,
							   INODE_TEST_DATA_BLK0);
	struct dentry dentry = { .d_sb = ctx->sb, .d_inode = inode };
	struct iattr attr = {};
	struct folio *folio;
	u8 *kaddr;
	u64 iv_before;
	int err;

	/* 1. S_IMMUTABLE and S_APPEND permission rejections */
	inode->i_flags |= S_IMMUTABLE;
	attr = (struct iattr){ .ia_valid = ATTR_MODE, .ia_mode = S_IFREG | 0600 };
	inode_lock(inode);
	KUNIT_EXPECT_EQ(test, ext4_setattr(&nop_mnt_idmap, &dentry, &attr),
			-EPERM);
	inode_unlock(inode);
	inode->i_flags &= ~S_IMMUTABLE;

	inode->i_flags |= S_APPEND;
	inode_lock(inode);
	KUNIT_EXPECT_EQ(test, ext4_setattr(&nop_mnt_idmap, &dentry, &attr),
			-EPERM);
	inode_unlock(inode);
	inode->i_flags &= ~S_APPEND;

	/* 2. Directory ATTR_SIZE -> -EINVAL; indirect > maxbytes -> -EFBIG */
	inode->i_mode = S_IFDIR | 0755;
	attr = (struct iattr){ .ia_valid = ATTR_SIZE, .ia_size = 0 };
	inode_lock(inode);
	KUNIT_EXPECT_EQ(test, ext4_setattr(&nop_mnt_idmap, &dentry, &attr),
			-EINVAL);
	inode_unlock(inode);

	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	memset(ctx->ei.i_data, 0, sizeof(ctx->ei.i_data));
	attr = (struct iattr){
		.ia_valid = ATTR_SIZE,
		.ia_size = ctx->sbi.s_bitmap_maxbytes + 4096,
	};
	inode_lock(inode);
	KUNIT_EXPECT_EQ(test, ext4_setattr(&nop_mnt_idmap, &dentry, &attr),
			-EFBIG);
	inode_unlock(inode);

	/* 3. ATTR_MODE chmod updates mode & increments iversion */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	iv_before = inode_peek_iversion(inode);
	attr = (struct iattr){ .ia_valid = ATTR_MODE, .ia_mode = S_IFREG | 0600 };
	inode_lock(inode);
	err = ext4_setattr(&nop_mnt_idmap, &dentry, &attr);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, inode->i_mode & 0777, 0600);
	KUNIT_EXPECT_GT(test, inode_peek_iversion(inode), iv_before);

	/*
	 * 4. ATTR_SIZE extend (2000 -> 8192) zeroes EOF tail [2000, 4096) in
	 *    mapped block 0 and advances i_size and i_disksize.
	 */
	inode->i_size = 2000;
	ctx->ei.i_disksize = 2000;
	inode->i_blocks = 16;
	inode_test_insert_leaf_extent(inode, 0, 2, INODE_TEST_DATA_BLK0, false);
	memset(dblk0->disk_buf, 0x77, INODE_TEST_BLK_SIZE);

	attr = (struct iattr){ .ia_valid = ATTR_SIZE, .ia_size = 8192 };
	inode_lock(inode);
	err = ext4_setattr(&nop_mnt_idmap, &dentry, &attr);
	inode_unlock(inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 8192LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 8192LL);

	folio = filemap_get_folio(inode->i_mapping, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	kaddr = kmap_local_folio(folio, 0);
	KUNIT_EXPECT_EQ(test, kaddr[1999], 0x77);
	KUNIT_EXPECT_EQ(test, kaddr[2000], 0x00);
	KUNIT_EXPECT_EQ(test, kaddr[4095], 0x00);
	kunmap_local(kaddr);
	folio_put(folio);

	/* 5. ATTR_SIZE shrink (8192 -> 4096) frees trailing block 1 */
	ctx->total_freed_blocks = 0;
	attr = (struct iattr){ .ia_valid = ATTR_SIZE, .ia_size = 4096 };
	inode_lock(inode);
	err = ext4_setattr(&nop_mnt_idmap, &dentry, &attr);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 4096LL);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 4096LL);
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blocks, 1UL);
}

/* -------------------------------------------------------------------------
 * 25. ext4_page_mkwrite() Delalloc, Block-Alloc, Fast-Path & Truncate Tests
 * -------------------------------------------------------------------------
 */

static vm_fault_t inode_test_call_page_mkwrite(struct vm_area_struct *vma,
					       struct folio *folio)
{
	struct vm_fault vmf = {
		.vma = vma,
		.page = folio_page(folio, 0),
	};

	return ext4_page_mkwrite(&vmf);
}

static void inode_test_page_mkwrite(struct kunit *test)
{
	struct inode_test_ctx *ctx = inode_test_setup(test);
	struct inode *inode = &ctx->ei.vfs_inode;
	struct file *dummy_file;
	struct vm_area_struct *vma;
	struct folio *folio;
	vm_fault_t ret;

	dummy_file = kunit_kzalloc(test, sizeof(*dummy_file), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dummy_file);
	dummy_file->f_inode = inode;
	dummy_file->f_mapping = inode->i_mapping;
	dummy_file->f_mode = FMODE_NOCMTIME;

	vma = kunit_kzalloc(test, sizeof(*vma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, vma);
	vma->vm_file = dummy_file;

	inode->i_size = 4096;
	ctx->ei.i_disksize = 4096;
	folio = __filemap_get_folio(inode->i_mapping, 0,
				    FGP_CREAT | FGP_LOCK, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	folio_zero_segment(folio, 0, folio_size(folio));
	folio_mark_uptodate(folio);
	folio_unlock(folio);

	/* 1. S_IMMUTABLE rejects write fault with VM_FAULT_SIGBUS */
	inode->i_flags |= S_IMMUTABLE;
	KUNIT_EXPECT_TRUE(test,
			  inode_test_call_page_mkwrite(vma, folio) ==
			  VM_FAULT_SIGBUS);
	inode->i_flags &= ~S_IMMUTABLE;

	/* 2. Delalloc fault reserves 1 delayed block & returns LOCKED */
	ret = inode_test_call_page_mkwrite(vma, folio);
	KUNIT_EXPECT_TRUE(test, ret == VM_FAULT_LOCKED);
	if (ret & VM_FAULT_LOCKED)
		folio_unlock(folio);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_reserved_data_blocks, 1U);
	folio_put(folio);

	/* 3. Non-delalloc fault allocates pblk; repeat fault hits fast-path */
	inode_test_reset_mock_inode(ctx, S_IFREG | 0644);
	clear_opt(ctx->sb, DELALLOC);
	ext4_set_aops(inode);
	inode->i_size = 4096;
	ctx->ei.i_disksize = 4096;
	ctx->next_alloc_pblk = INODE_TEST_DATA_BLK0;
	ctx->alloc_calls = 0;

	folio = __filemap_get_folio(inode->i_mapping, 0,
				    FGP_CREAT | FGP_LOCK, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	folio_zero_segment(folio, 0, folio_size(folio));
	folio_mark_uptodate(folio);
	folio_unlock(folio);

	ret = inode_test_call_page_mkwrite(vma, folio);
	KUNIT_EXPECT_TRUE(test, ret == VM_FAULT_LOCKED);
	if (ret & VM_FAULT_LOCKED)
		folio_unlock(folio);
	KUNIT_EXPECT_EQ(test, ctx->alloc_calls, 1U);

	/* Second fault sees all buffers mapped and skips journal_start/alloc */
	ret = inode_test_call_page_mkwrite(vma, folio);
	KUNIT_EXPECT_TRUE(test, ret == VM_FAULT_LOCKED);
	if (ret & VM_FAULT_LOCKED)
		folio_unlock(folio);
	KUNIT_EXPECT_EQ(test, ctx->alloc_calls, 1U);

	/* 4. Folio beyond truncated i_size returns VM_FAULT_NOPAGE */
	folio_put(folio);
	folio = __filemap_get_folio(inode->i_mapping, 2,
				    FGP_CREAT | FGP_LOCK, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	folio_unlock(folio);
	KUNIT_EXPECT_TRUE(test,
			  inode_test_call_page_mkwrite(vma, folio) ==
			  VM_FAULT_NOPAGE);
	folio_put(folio);
	set_opt(ctx->sb, DELALLOC);
}

static struct kunit_case ext4_inode_test_cases[] = {
	KUNIT_CASE_PARAM(inode_test_xtimestamp_decoding, ext4_inode_gen_params),
	KUNIT_CASE(inode_test_xtimestamp_encoding_roundtrip),
	KUNIT_CASE(inode_test_csum_set_and_verify),
	KUNIT_CASE(inode_test_blocks_encode_decode),
	KUNIT_CASE(inode_test_fill_raw_inode),
	KUNIT_CASE(inode_test_mark_dirty_and_update),
	KUNIT_CASE(inode_test_expand_extra_isize),
	KUNIT_CASE(inode_test_get_inode_loc_variants),
	KUNIT_CASE(inode_test_iget_validation_and_modes),
	KUNIT_CASE(inode_test_iget_corruptions),
	KUNIT_CASE(inode_test_fast_symlink_and_can_truncate),
	KUNIT_CASE(inode_test_flags_aops_mapping_order),
	KUNIT_CASE(inode_test_projid_dio_and_getattr),
	KUNIT_CASE(inode_test_da_reserve_and_release),
	KUNIT_CASE(inode_test_da_bigalloc_clusters),
	KUNIT_CASE(inode_test_map_blocks_paths),
	KUNIT_CASE(inode_test_get_block_bread_and_walk_buffers),
	KUNIT_CASE(inode_test_iomap_and_atomic_writes),
	KUNIT_CASE(inode_test_trans_credits_and_disksize),
	KUNIT_CASE(inode_test_zero_partial_and_eof),
	KUNIT_CASE(inode_test_punch_hole_and_truncate),
	KUNIT_CASE(inode_test_write_and_sync_metadata),
	KUNIT_CASE(inode_test_evict_inode_paths),
	KUNIT_CASE(inode_test_buffered_write_and_folio_ops),
	KUNIT_CASE(inode_test_setattr),
	KUNIT_CASE(inode_test_page_mkwrite),
	{}
};

static struct kunit_suite ext4_inode_test_suite = {
	.name = "ext4_inode_test",
	.test_cases = ext4_inode_test_cases,
};

kunit_test_suites(&ext4_inode_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 inode operations");
MODULE_LICENSE("GPL v2");
