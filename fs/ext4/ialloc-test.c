// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/ialloc.c
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/posix_acl.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timekeeping.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "xattr.h"
#include "acl.h"

/*
 * Multi-group mock filesystem geometry:
 *  - 4 block groups, 1024-byte blocks (8192 bits per bitmap block)
 *  - 64 inodes per group (256 total inodes), 256-byte inodes (4 per block)
 *  - 16 inode table blocks per group, 1024 blocks (clusters) per group
 *  - s_first_data_block = 1, s_first_ino = 11 (inodes 1..10 reserved in bg 0)
 *  - sparse_super enabled: groups 0, 1, 3 have backup super + 1 gdb (2 base
 *    clusters), while group 2 has 0 base clusters (18 total overhead clusters,
 *    leaving 1024 - 18 = 1006 free clusters after init).
 */
#define IALLOC_TEST_NGROUPS		4U
#define IALLOC_TEST_BLK_SIZE		1024U
#define IALLOC_TEST_BLK_BITS		10U
#define IALLOC_TEST_BLOCKS_PER_GRP	1024U
#define IALLOC_TEST_FIRST_DATA_BLK	1U
#define IALLOC_TEST_TOTAL_BLKS		(IALLOC_TEST_FIRST_DATA_BLK + \
					 IALLOC_TEST_NGROUPS * IALLOC_TEST_BLOCKS_PER_GRP)
#define IALLOC_TEST_INODE_SIZE		256U
#define IALLOC_TEST_EXTRA_ISIZE		32U
#define IALLOC_TEST_INODES_PER_BLK	(IALLOC_TEST_BLK_SIZE / IALLOC_TEST_INODE_SIZE)
#define IALLOC_TEST_INODES_PER_GRP	64U
#define IALLOC_TEST_ITB_PER_GRP		(IALLOC_TEST_INODES_PER_GRP / \
					 IALLOC_TEST_INODES_PER_BLK)
#define IALLOC_TEST_TOTAL_INODES	(IALLOC_TEST_NGROUPS * IALLOC_TEST_INODES_PER_GRP)
#define IALLOC_TEST_FIRST_INO		11U
#define IALLOC_TEST_BG0_FREE_INODES	(IALLOC_TEST_INODES_PER_GRP - \
					 (IALLOC_TEST_FIRST_INO - 1U))
#define IALLOC_TEST_DEFAULT_FREE_CLU	800U
#define IALLOC_TEST_CSUM_SEED		0x6d4b29a3U

#define IALLOC_TEST_GDESC_BLK		2ULL
#define IALLOC_TEST_POOL_SIZE		32U
#define IALLOC_TEST_MAX_TRACKED_INODES	32U

struct ialloc_test_blk {
	bool in_use;
	bool in_cache;
	bool marked_dirty;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	char cache_buf[IALLOC_TEST_BLK_SIZE] __aligned(8);
	char disk_buf[IALLOC_TEST_BLK_SIZE] __aligned(8);
};

struct ialloc_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct blockgroup_lock bgl;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct buffer_head sb_bh;
	struct buffer_head gdesc_bh;
	char gdesc_buf[IALLOC_TEST_BLK_SIZE] __aligned(8);
	bool gdesc_marked_dirty;

	struct ext4_group_desc gdps[IALLOC_TEST_NGROUPS];
	struct ext4_group_info *grp_infos[IALLOC_TEST_NGROUPS];
	struct ext4_group_info *grp_info_ptrs[16];
	struct ext4_group_info **grp_info_slice[1];

	struct flex_groups flex_grps[IALLOC_TEST_NGROUPS];
	struct flex_groups *flex_grp_ptrs[IALLOC_TEST_NGROUPS];

	struct inode *root_inode;

	struct ext4_inode_info standalone_ei;
	struct inode *tracked_inodes[IALLOC_TEST_MAX_TRACKED_INODES];
	unsigned int nr_tracked_inodes;
	bool evict_skip_free;

	struct ialloc_test_blk pool[IALLOC_TEST_POOL_SIZE];

	/* Fault injection and call tracking */
	u32 fail_gdp_mask;
	bool fail_getblk;
	bool fail_read_eio;
	bool fail_alloc_inode;
	bool fail_sync_dirty_buffer;
	int fail_bbitmap_err;
	int fail_wait_bbitmap_err;
	int fail_write_access_err;
	int fail_write_access_on_call;
	int fail_dirty_metadata_err;
	int fail_dirty_metadata_on_call;
	int fail_mark_inode_dirty_err;
#ifdef CONFIG_EXT4_FS_SECURITY
	int fail_init_security_err;
#endif
	int fail_itable_zeroout_err;
	int fail_iget_err;
	struct inode *iget_inode;

	unsigned int read_bh_calls;
	unsigned int write_access_calls;
	unsigned int dirty_metadata_calls;
	unsigned int zeroout_calls;
	ext4_fsblk_t last_zeroout_blk;
	int last_zeroout_num;
	int last_zeroout_barrier;
};

static inline ext4_fsblk_t ialloc_test_group_start(ext4_group_t g)
{
	return IALLOC_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * IALLOC_TEST_BLOCKS_PER_GRP;
}

static inline ext4_fsblk_t ialloc_test_bbitmap_blk(ext4_group_t g)
{
	return ialloc_test_group_start(g) + 2;
}

static inline ext4_fsblk_t ialloc_test_ibitmap_blk(ext4_group_t g)
{
	return ialloc_test_group_start(g) + 3;
}

static inline ext4_fsblk_t ialloc_test_itable_blk(ext4_group_t g)
{
	return ialloc_test_group_start(g) + 4;
}

static inline void clear_bitmap_uptodate(struct buffer_head *bh)
{
	clear_bit(BH_BITMAP_UPTODATE, &bh->b_state);
}

static void ialloc_test_init_ei_fields(struct ext4_inode_info *ei)
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
#ifdef CONFIG_FS_ENCRYPTION
	ei->i_crypt_info = NULL;
#endif
	ei->jinode = NULL;
	init_rwsem(&ei->xattr_sem);
	INIT_LIST_HEAD(&ei->i_rsv_conversion_list);
	spin_lock_init(&ei->i_completed_io_lock);
	ei->i_sync_tid = 0;
	ei->i_datasync_tid = 0;
	INIT_LIST_HEAD(&ei->i_orphan);
	ext4_fc_init_inode(&ei->vfs_inode);
	spin_lock_init(&ei->i_fc_lock);
	ei->i_metadata_bhs = NULL;
	ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	ei->i_extra_isize = IALLOC_TEST_EXTRA_ISIZE;
	ei->i_csum_seed = IALLOC_TEST_CSUM_SEED;
	ei->i_last_alloc_group = ~0U;
}

static struct inode *ialloc_test_alloc_inode(struct super_block *sb)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ext4_inode_info *ei;

	if (ctx && ctx->fail_alloc_inode)
		return NULL;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	ialloc_test_init_ei_fields(ei);
	return &ei->vfs_inode;
}

static void ialloc_test_evict_inode(struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	truncate_inode_pages_final(&inode->i_data);
	if (ctx && !ctx->evict_skip_free &&
	    !inode->i_nlink && !is_bad_inode(inode)) {
		ext4_free_inode(NULL, inode);
		return;
	}
	ext4_clear_inode(inode);
}

static void ialloc_test_destroy_inode(struct inode *inode)
{
	kfree(ext4_i_metadata_bhs(inode));
	EXT4_I(inode)->i_metadata_bhs = NULL;
}

static void ialloc_test_free_inode_mem(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **ialloc_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *ialloc_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations ialloc_test_qops = {
	.get_reserved_space = ialloc_test_get_reserved_space,
};
#endif

static const struct super_operations ialloc_test_sops = {
	.alloc_inode	= ialloc_test_alloc_inode,
	.evict_inode	= ialloc_test_evict_inode,
	.destroy_inode	= ialloc_test_destroy_inode,
	.free_inode	= ialloc_test_free_inode_mem,
	.drop_inode	= inode_just_drop,
#ifdef CONFIG_QUOTA
	.get_dquots	= ialloc_test_get_dquots,
#endif
};

static int ialloc_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type ialloc_test_fs_type = {
	.name		 = "ext4_ialloc_kunit",
	.init_fs_context = ialloc_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static struct ialloc_test_blk *ialloc_test_find_blk(struct ialloc_test_ctx *ctx,
						    ext4_fsblk_t blocknr)
{
	unsigned int i;

	for (i = 0; i < IALLOC_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && ctx->pool[i].blocknr == blocknr)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct ialloc_test_blk *ialloc_test_find_blk_by_bh(
		struct ialloc_test_ctx *ctx, struct buffer_head *bh)
{
	unsigned int i;

	for (i = 0; i < IALLOC_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && &ctx->pool[i].bh == bh)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct ialloc_test_blk *ialloc_test_setup_blk(struct ialloc_test_ctx *ctx,
						     ext4_fsblk_t blocknr,
						     bool in_cache,
						     bool uptodate)
{
	struct ialloc_test_blk *slot = ialloc_test_find_blk(ctx, blocknr);
	unsigned int i;

	if (!slot) {
		for (i = 0; i < IALLOC_TEST_POOL_SIZE; i++) {
			if (!ctx->pool[i].in_use) {
				slot = &ctx->pool[i];
				break;
			}
		}
	}
	if (!slot)
		return NULL;

	memset(slot, 0, sizeof(*slot));
	slot->in_use = true;
	slot->in_cache = in_cache;
	slot->blocknr = blocknr;
	slot->bh.b_bdev = &ctx->bdev;
	slot->bh.b_blocknr = blocknr;
	slot->bh.b_size = IALLOC_TEST_BLK_SIZE;
	slot->bh.b_data = slot->cache_buf;
	atomic_set(&slot->bh.b_count, 1);
	if (uptodate)
		set_buffer_uptodate(&slot->bh);
	return slot;
}

/* --- Static Stubs --- */

static struct ext4_group_desc *ialloc_test_get_group_desc_stub(
		struct super_block *sb, ext4_group_t block_group,
		struct buffer_head **bh)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx || block_group >= EXT4_SB(sb)->s_groups_count)
		return NULL;
	if (block_group < 32 && (ctx->fail_gdp_mask & BIT(block_group)))
		return NULL;
	if (bh)
		*bh = &ctx->gdesc_bh;
	return &ctx->gdps[block_group];
}

static struct buffer_head *ialloc_test_sb_getblk_stub(struct super_block *sb,
						      ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ialloc_test_blk *slot;

	if (!ctx || ctx->fail_getblk)
		return NULL;
	slot = ialloc_test_find_blk(ctx, block);
	if (!slot)
		return NULL;
	slot->in_cache = true;
	get_bh(&slot->bh);
	return &slot->bh;
}

static struct buffer_head *ialloc_test_sb_find_get_block_stub(
		struct super_block *sb, ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ialloc_test_blk *slot;

	if (!ctx)
		return NULL;
	slot = ialloc_test_find_blk(ctx, block);
	if (!slot || !slot->in_cache)
		return NULL;
	get_bh(&slot->bh);
	return &slot->bh;
}

static int ialloc_test_read_bh_stub(struct buffer_head *bh, blk_opf_t op_flags,
				    bio_end_io_t end_io, bool simu_fail)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ialloc_test_blk *slot;
	bool fail;

	if (!ctx) {
		unlock_buffer(bh);
		return -EIO;
	}
	ctx->read_bh_calls++;
	slot = ialloc_test_find_blk_by_bh(ctx, bh);
	fail = ctx->fail_read_eio || simu_fail || !slot;

	if (!fail)
		memcpy(bh->b_data, slot->disk_buf, bh->b_size);

	if (end_io) {
		struct bio *bio = bio_alloc(NULL, 0, REQ_OP_READ, GFP_KERNEL);

		if (bio) {
			bio->bi_private = bh;
			bio->bi_status = fail ? BLK_STS_IOERR : BLK_STS_OK;
			end_io(bio);
			return fail ? -EIO : 0;
		}
	}

	if (fail) {
		clear_buffer_uptodate(bh);
	} else {
		set_buffer_uptodate(bh);
		set_bitmap_uptodate(bh);
	}
	unlock_buffer(bh);
	return fail ? -EIO : 0;
}

static struct buffer_head *ialloc_test_read_bbitmap_nowait_stub(
		struct super_block *sb, ext4_group_t block_group,
		bool ignore_locked)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ialloc_test_blk *slot;

	if (!ctx)
		return ERR_PTR(-EIO);
	if (ctx->fail_bbitmap_err)
		return ERR_PTR(ctx->fail_bbitmap_err);
	slot = ialloc_test_find_blk(ctx, ialloc_test_bbitmap_blk(block_group));
	if (!slot)
		return ERR_PTR(-ENOMEM);
	set_buffer_uptodate(&slot->bh);
	set_bitmap_uptodate(&slot->bh);
	set_buffer_verified(&slot->bh);
	get_bh(&slot->bh);
	return &slot->bh;
}

static int ialloc_test_wait_bbitmap_stub(struct super_block *sb,
					 ext4_group_t block_group,
					 struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;
	return ctx->fail_wait_bbitmap_err;
}

static int ialloc_test_get_write_access_stub(
		const char *where, unsigned int line, handle_t *handle,
		struct super_block *sb, struct buffer_head *bh,
		enum ext4_journal_trigger_type trigger_type)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;
	ctx->write_access_calls++;
	if (ctx->fail_write_access_err &&
	    (!ctx->fail_write_access_on_call ||
	     ctx->write_access_calls == ctx->fail_write_access_on_call))
		return ctx->fail_write_access_err;
	return 0;
}

static int ialloc_test_dirty_metadata_stub(const char *where, unsigned int line,
					   handle_t *handle,
					   struct inode *inode,
					   struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;
	struct ialloc_test_blk *slot;

	if (!ctx)
		return 0;
	ctx->dirty_metadata_calls++;
	if (ctx->fail_dirty_metadata_err &&
	    (!ctx->fail_dirty_metadata_on_call ||
	     ctx->dirty_metadata_calls == ctx->fail_dirty_metadata_on_call))
		return ctx->fail_dirty_metadata_err;

	if (bh) {
		set_buffer_uptodate(bh);
		if (bh == &ctx->gdesc_bh) {
			ctx->gdesc_marked_dirty = true;
		} else {
			slot = ialloc_test_find_blk_by_bh(ctx, bh);
			if (slot)
				slot->marked_dirty = true;
		}
		if (ctx->fail_sync_dirty_buffer) {
			clear_buffer_mapped(bh);
			set_buffer_dirty(bh);
		} else {
			clear_buffer_dirty(bh);
		}
	}
	return 0;
}

static int ialloc_test_mark_inode_dirty_stub(handle_t *handle,
					     struct inode *inode,
					     const char *func,
					     unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;
	return ctx->fail_mark_inode_dirty_err;
}

#ifdef CONFIG_EXT4_FS_SECURITY
static int ialloc_test_init_security_stub(handle_t *handle,
					  struct inode *inode,
					  struct inode *dir,
					  const struct qstr *qstr)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;
	return ctx->fail_init_security_err;
}
#endif

static int ialloc_test_itable_zeroout_stub(struct super_block *sb,
					   ext4_fsblk_t blk, int num,
					   int barrier)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;
	ctx->zeroout_calls++;
	ctx->last_zeroout_blk = blk;
	ctx->last_zeroout_num = num;
	ctx->last_zeroout_barrier = barrier;
	return ctx->fail_itable_zeroout_err;
}

static struct inode *ialloc_test_iget_stub(struct super_block *sb,
					   unsigned long ino,
					   ext4_iget_flags flags,
					   const char *function,
					   unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct ialloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return ERR_PTR(-EIO);
	if (ctx->fail_iget_err)
		return ERR_PTR(ctx->fail_iget_err);
	if (!ctx->iget_inode)
		return ERR_PTR(-ENOENT);
	ihold(ctx->iget_inode);
	return ctx->iget_inode;
}

/* --- Fixture Helpers & Cleanup --- */

static void ialloc_test_track_inode(struct kunit *test,
				    struct ialloc_test_ctx *ctx,
				    struct inode *inode)
{
	if (inode_state_read_once(inode) & I_NEW)
		unlock_new_inode(inode);
	KUNIT_ASSERT_LT(test, ctx->nr_tracked_inodes,
			IALLOC_TEST_MAX_TRACKED_INODES);
	ctx->tracked_inodes[ctx->nr_tracked_inodes++] = inode;
}

static void ialloc_test_untrack_inode(struct ialloc_test_ctx *ctx,
				      struct inode *inode)
{
	unsigned int i;

	for (i = 0; i < ctx->nr_tracked_inodes; i++) {
		if (ctx->tracked_inodes[i] == inode) {
			ctx->tracked_inodes[i] =
				ctx->tracked_inodes[--ctx->nr_tracked_inodes];
			ctx->tracked_inodes[ctx->nr_tracked_inodes] = NULL;
			return;
		}
	}
}

static void ialloc_test_sync_ibitmap_csum(struct ialloc_test_ctx *ctx,
					  ext4_group_t g, bool sync_disk)
{
	struct ialloc_test_blk *slot;

	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(g));
	if (!slot)
		return;
	ext4_inode_bitmap_csum_set(ctx->sb, &ctx->gdps[g], &slot->bh);
	ext4_group_desc_csum_set(ctx->sb, g, &ctx->gdps[g]);
	if (sync_disk)
		memcpy(slot->disk_buf, slot->cache_buf, IALLOC_TEST_BLK_SIZE);
}

static void ialloc_test_enable_flex_bg(struct ialloc_test_ctx *ctx,
				       unsigned int log_groups_per_flex)
{
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	ext4_group_t g, fg;

	ext4_set_feature_flex_bg(sb);
	sbi->s_log_groups_per_flex = log_groups_per_flex;
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++) {
		atomic_set(&ctx->flex_grps[g].free_inodes, 0);
		atomic_set(&ctx->flex_grps[g].used_dirs, 0);
		atomic64_set(&ctx->flex_grps[g].free_clusters, 0);
	}
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++) {
		fg = ext4_flex_group(sbi, g);
		atomic_add(ext4_free_inodes_count(sb, &ctx->gdps[g]),
			   &ctx->flex_grps[fg].free_inodes);
		atomic_add(ext4_used_dirs_count(sb, &ctx->gdps[g]),
			   &ctx->flex_grps[fg].used_dirs);
		atomic64_add(ext4_free_group_clusters(sb, &ctx->gdps[g]),
			     &ctx->flex_grps[fg].free_clusters);
	}
}

static void ialloc_test_prep_standalone_free_inode(struct ialloc_test_ctx *ctx,
						   unsigned long ino,
						   umode_t mode)
{
	struct ext4_inode_info *ei = &ctx->standalone_ei;
	struct inode *inode = &ei->vfs_inode;

	memset(ei, 0, sizeof(*ei));
	init_rwsem(&ei->i_data_sem);
	inode_init_once(inode);
	inode->i_sb = ctx->sb;
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = ctx->sb->s_blocksize_bits;
	inode->i_flags = S_NOQUOTA;
	inode->i_mapping = &inode->i_data;
	inode->i_data.host = inode;
	mapping_set_gfp_mask(&inode->i_data, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&inode->i_rwsem);
	init_rwsem(&inode->i_data.invalidate_lock);
	spin_lock_init(&inode->i_lock);
	inode_state_assign_raw(inode, I_FREEING);
	inode->__i_nlink = 0;
	atomic_set(&inode->i_count, 1);
	ialloc_test_init_ei_fields(ei);
}

static void ialloc_test_cleanup(void *data)
{
	struct ialloc_test_ctx *ctx = data;
	handle_t *handle;
	unsigned int i;

	ctx->evict_skip_free = true;
	ctx->sbi.s_journal = NULL;

	while ((handle = ext4_journal_current_handle()) != NULL)
		ext4_journal_stop(handle);

	for (i = 0; i < ctx->nr_tracked_inodes; i++) {
		struct inode *ti = ctx->tracked_inodes[i];

		if (ti) {
			if (inode_state_read_once(ti) & I_NEW)
				unlock_new_inode(ti);
			spin_lock(&ti->i_lock);
			inode_state_clear(ti, I_DIRTY_TIME);
			spin_unlock(&ti->i_lock);
			set_nlink(ti, 1);
			iput(ti);
			ctx->tracked_inodes[i] = NULL;
		}
	}
	ctx->nr_tracked_inodes = 0;

	if (ctx->root_inode) {
		set_nlink(ctx->root_inode, 2);
		ctx->root_inode = NULL;
	}

	deactivate_super(ctx->sb);
	rcu_barrier();

	percpu_counter_destroy(&ctx->sbi.s_dirs_counter);
	percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
	percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);

	kfree(ctx);
}

static int ialloc_test_init(struct kunit *test)
{
	struct ialloc_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	ext4_group_t g;
	unsigned int bit;
	s64 total_free_inodes = 0;
	s64 total_free_clusters = 0;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	fc = fs_context_for_mount(&ialloc_test_fs_type, 0);
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
	sb->s_op = &ialloc_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &ialloc_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = IALLOC_TEST_BLK_SIZE;
	sb->s_blocksize_bits = IALLOC_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_ialloc_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = IALLOC_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(IALLOC_TEST_FIRST_DATA_BLK);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	ctx->es.s_inodes_count = cpu_to_le32(IALLOC_TEST_TOTAL_INODES);
	ctx->es.s_inodes_per_group = cpu_to_le32(IALLOC_TEST_INODES_PER_GRP);
	ctx->es.s_blocks_per_group = cpu_to_le32(IALLOC_TEST_BLOCKS_PER_GRP);
	ctx->es.s_clusters_per_group = cpu_to_le32(IALLOC_TEST_BLOCKS_PER_GRP);
	ctx->es.s_first_ino = cpu_to_le32(IALLOC_TEST_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(IALLOC_TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(IALLOC_TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(IALLOC_TEST_EXTRA_ISIZE);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_blocks_count_set(&ctx->es, IALLOC_TEST_TOTAL_BLKS);

	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_extents(sb);
	ext4_set_feature_extra_isize(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);
	ext4_set_feature_dir_index(sb);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	ctx->gdesc_bh.b_data = ctx->gdesc_buf;
	ctx->gdesc_bh.b_size = IALLOC_TEST_BLK_SIZE;
	ctx->gdesc_bh.b_blocknr = IALLOC_TEST_GDESC_BLK;
	ctx->gdesc_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->gdesc_bh.b_count, 1);
	set_buffer_uptodate(&ctx->gdesc_bh);

	sbi->s_inode_size = IALLOC_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = IALLOC_TEST_INODES_PER_BLK;
	sbi->s_inodes_per_group = IALLOC_TEST_INODES_PER_GRP;
	sbi->s_itb_per_group = IALLOC_TEST_ITB_PER_GRP;
	sbi->s_first_ino = IALLOC_TEST_FIRST_INO;
	sbi->s_want_extra_isize = IALLOC_TEST_EXTRA_ISIZE;
	sbi->s_blocks_per_group = IALLOC_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = IALLOC_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = IALLOC_TEST_NGROUPS;
	sbi->s_gdb_count = 1;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = IALLOC_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block_bits = ilog2(sbi->s_desc_per_block);
	sbi->s_bitmap_maxbytes = 0x1ff7ffff000ULL;
	sbi->s_csum_seed = IALLOC_TEST_CSUM_SEED;
	sbi->s_hash_seed[0] = 0x12345678U;
	sbi->s_hash_seed[1] = 0x9abcdef0U;
	sbi->s_hash_seed[2] = 0x13579bdfU;
	sbi->s_hash_seed[3] = 0x2468ace0U;

	set_opt(sb, ERRORS_CONT);
	set_opt(sb, ORDERED_DATA);
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

	/* Wire RCU-indexed group_info and flex_groups tables */
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++) {
		struct ialloc_test_blk *iblk, *bblk;
		u32 free_inos = (g == 0) ? IALLOC_TEST_BG0_FREE_INODES :
					   IALLOC_TEST_INODES_PER_GRP;

		ctx->grp_infos[g] = kunit_kzalloc(test,
						  sizeof(*ctx->grp_infos[g]),
						  GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, ctx->grp_infos[g]);
		init_rwsem(&ctx->grp_infos[g]->alloc_sem);
		ctx->grp_infos[g]->bb_state = 0;
		ctx->grp_info_ptrs[g] = ctx->grp_infos[g];
		ctx->flex_grp_ptrs[g] = &ctx->flex_grps[g];

		ext4_block_bitmap_set(sb, &ctx->gdps[g],
				      ialloc_test_bbitmap_blk(g));
		ext4_inode_bitmap_set(sb, &ctx->gdps[g],
				      ialloc_test_ibitmap_blk(g));
		ext4_inode_table_set(sb, &ctx->gdps[g],
				     ialloc_test_itable_blk(g));
		ext4_free_inodes_set(sb, &ctx->gdps[g], free_inos);
		ext4_used_dirs_set(sb, &ctx->gdps[g], (g == 0) ? 1U : 0U);
		ext4_itable_unused_set(sb, &ctx->gdps[g], free_inos);
		ext4_free_group_clusters_set(sb, &ctx->gdps[g],
					     IALLOC_TEST_DEFAULT_FREE_CLU);

		iblk = ialloc_test_setup_blk(ctx, ialloc_test_ibitmap_blk(g),
					     true, true);
		bblk = ialloc_test_setup_blk(ctx, ialloc_test_bbitmap_blk(g),
					     true, true);
		if (!iblk || !bblk) {
			deactivate_locked_super(sb);
			kfree(ctx);
			KUNIT_FAIL_AND_ABORT(test, "block pool setup failed");
		}

		/* Mark reserved bits 0..9 in group 0 and end padding 64..8191 */
		if (g == 0) {
			for (bit = 0; bit < IALLOC_TEST_FIRST_INO - 1; bit++)
				ext4_set_bit(bit, iblk->cache_buf);
		}
		ext4_mark_bitmap_end(IALLOC_TEST_INODES_PER_GRP,
				     IALLOC_TEST_BLK_SIZE * 8,
				     iblk->cache_buf);
		set_bitmap_uptodate(&iblk->bh);
		set_buffer_verified(&iblk->bh);

		ext4_block_bitmap_csum_set(sb, &ctx->gdps[g], &bblk->bh);
		memcpy(bblk->disk_buf, bblk->cache_buf, IALLOC_TEST_BLK_SIZE);
		set_bitmap_uptodate(&bblk->bh);
		set_buffer_verified(&bblk->bh);

		ialloc_test_sync_ibitmap_csum(ctx, g, true);

		total_free_inodes += free_inos;
		total_free_clusters += IALLOC_TEST_DEFAULT_FREE_CLU;
	}
	ctx->grp_info_slice[0] = ctx->grp_info_ptrs;
	rcu_assign_pointer(sbi->s_group_info, ctx->grp_info_slice);
	rcu_assign_pointer(sbi->s_flex_groups, ctx->flex_grp_ptrs);

	err = percpu_counter_init(&sbi->s_freeclusters_counter,
				  total_free_clusters, GFP_KERNEL);
	if (err) {
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = percpu_counter_init(&sbi->s_freeinodes_counter,
				  total_free_inodes, GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = percpu_counter_init(&sbi->s_dirs_counter, 1, GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}

	/* Create hashed VFS root directory inode (EXT4_ROOT_INO = 2) */
	ctx->root_inode = new_inode(sb);
	if (!ctx->root_inode) {
		percpu_counter_destroy(&ctx->sbi.s_dirs_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		deactivate_locked_super(sb);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to allocate root_inode");
	}
	ctx->root_inode->i_ino = EXT4_ROOT_INO;
	ctx->root_inode->i_mode = S_IFDIR | 0755;
	ctx->root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(ctx->root_inode, 2);
	EXT4_I(ctx->root_inode)->i_block_group = 0;
	insert_inode_locked(ctx->root_inode);
	unlock_new_inode(ctx->root_inode);

	sb->s_root = d_make_root(ctx->root_inode);
	if (!sb->s_root) {
		percpu_counter_destroy(&ctx->sbi.s_dirs_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		deactivate_locked_super(sb);
		rcu_barrier();
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to allocate s_root");
	}

	up_write(&sb->s_umount);

	err = kunit_add_action_or_reset(test, ialloc_test_cleanup, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	kunit_activate_static_stub(test, ext4_get_group_desc,
				   ialloc_test_get_group_desc_stub);
	kunit_activate_static_stub(test, ext4_sb_getblk,
				   ialloc_test_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_sb_find_get_block,
				   ialloc_test_sb_find_get_block_stub);
	kunit_activate_static_stub(test, ext4_read_bh,
				   ialloc_test_read_bh_stub);
	kunit_activate_static_stub(test, ext4_read_block_bitmap_nowait,
				   ialloc_test_read_bbitmap_nowait_stub);
	kunit_activate_static_stub(test, ext4_wait_block_bitmap,
				   ialloc_test_wait_bbitmap_stub);
	kunit_activate_static_stub(test, __ext4_journal_get_write_access,
				   ialloc_test_get_write_access_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   ialloc_test_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   ialloc_test_mark_inode_dirty_stub);
#ifdef CONFIG_EXT4_FS_SECURITY
	kunit_activate_static_stub(test, ext4_init_security,
				   ialloc_test_init_security_stub);
#endif
	kunit_activate_static_stub(test, ext4_itable_zeroout,
				   ialloc_test_itable_zeroout_stub);
	kunit_activate_static_stub(test, __ext4_iget,
				   ialloc_test_iget_stub);

	return 0;
}

/* =========================================================================
 * 1. Bitmap Helpers: ext4_mark_bitmap_end & ext4_end_bitmap_read
 * =========================================================================
 */

struct mark_bitmap_end_param {
	const char *desc;
	int start_bit;
	int end_bit;
};

static const struct mark_bitmap_end_param mark_bitmap_end_params[] = {
	{ "unaligned_start_to_aligned_end", 67, 128 },
	{ "byte_aligned_memset_only", 64, 128 },
	{ "single_partial_byte_no_memset", 3, 8 },
	{ "unaligned_within_second_byte", 10, 16 },
	{ "equal_start_and_end_noop", 64, 64 },
	{ "start_greater_than_end_noop", 75, 64 },
};

static void mark_bitmap_end_param_to_desc(const struct mark_bitmap_end_param *p,
					  char *desc)
{
	strscpy(desc, p->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(mark_bitmap_end, mark_bitmap_end_params,
		  mark_bitmap_end_param_to_desc);

static void ialloc_test_mark_bitmap_end(struct kunit *test)
{
	const struct mark_bitmap_end_param *p = test->param_value;
	char bitmap[24] = {};
	int bit;

	ext4_mark_bitmap_end(p->start_bit, p->end_bit, bitmap);

	for (bit = 0; bit < (int)sizeof(bitmap) * 8; bit++) {
		bool expected = (p->start_bit < p->end_bit) &&
				(bit >= p->start_bit && bit < p->end_bit);

		KUNIT_EXPECT_EQ_MSG(test, (bool)ext4_test_bit(bit, bitmap),
				    expected, "%s: unexpected value at bit %d",
				    p->desc, bit);
	}
}

static void ialloc_test_end_bitmap_read_ok(struct kunit *test)
{
	struct buffer_head bh = {};
	struct bio *bio;

	atomic_set(&bh.b_count, 1);
	lock_buffer(&bh);

	bio = bio_alloc(NULL, 0, REQ_OP_READ, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bio);
	bio->bi_private = &bh;
	bio->bi_status = BLK_STS_OK;

	ext4_end_bitmap_read(bio);

	KUNIT_EXPECT_FALSE(test, buffer_locked(&bh));
	KUNIT_EXPECT_TRUE(test, buffer_uptodate(&bh));
	KUNIT_EXPECT_TRUE(test, bitmap_uptodate(&bh));
}

static void ialloc_test_end_bitmap_read_ioerr(struct kunit *test)
{
	struct buffer_head bh = {};
	struct bio *bio;

	atomic_set(&bh.b_count, 1);
	lock_buffer(&bh);

	bio = bio_alloc(NULL, 0, REQ_OP_READ, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bio);
	bio->bi_private = &bh;
	bio->bi_status = BLK_STS_IOERR;
	bio_set_flag(bio, BIO_QUIET);

	ext4_end_bitmap_read(bio);

	KUNIT_EXPECT_FALSE(test, buffer_locked(&bh));
	KUNIT_EXPECT_FALSE(test, buffer_uptodate(&bh));
	KUNIT_EXPECT_FALSE(test, bitmap_uptodate(&bh));
	KUNIT_EXPECT_TRUE(test, test_bit(BH_Quiet, &bh.b_state));
}

/* =========================================================================
 * 2. Inode Bitmap Validation: ext4_validate_inode_bitmap_test
 * =========================================================================
 */

static void ialloc_test_validate_ibitmap_csum(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *slot;
	s64 free_before;
	int err;

	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* 64-bit group descriptor valid CRC */
	clear_buffer_verified(&slot->bh);
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[1], 1, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, (bool)buffer_verified(&slot->bh));
	KUNIT_EXPECT_FALSE(test,
			   (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));

	/* 32-bit group descriptor valid CRC (low 16-bit checksum only) */
	ext4_clear_feature_64bit(sb);
	ctx->sbi.s_desc_size = EXT4_MIN_DESC_SIZE;
	ext4_inode_bitmap_csum_set(sb, &ctx->gdps[1], &slot->bh);
	clear_buffer_verified(&slot->bh);
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[1], 1, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, (bool)buffer_verified(&slot->bh));

	/* Restore 64-bit descriptor and corrupt bitmap byte */
	ext4_set_feature_64bit(sb);
	ctx->sbi.s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	ialloc_test_sync_ibitmap_csum(ctx, 1, false);
	clear_buffer_verified(&slot->bh);
	slot->cache_buf[0] ^= 0x01;
	free_before = percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter);

	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[1], 1, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, -EFSBADCRC);
	KUNIT_EXPECT_FALSE(test, (bool)buffer_verified(&slot->bh));
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter),
			free_before - IALLOC_TEST_INODES_PER_GRP);
}

static void ialloc_test_validate_ibitmap_short_circuits(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *slot;
	int err;

	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* Corrupt byte, but EXT4_FC_REPLAY skips validation */
	clear_buffer_verified(&slot->bh);
	slot->cache_buf[0] ^= 0xff;
	ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[2], 2, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, (bool)buffer_verified(&slot->bh));
	ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;

	/* Already buffer_verified short-circuits before checking CRC */
	set_buffer_verified(&slot->bh);
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[2], 2, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, 0);

	/* Out-of-range group (NULL ext4_get_group_info) -> -EFSCORRUPTED */
	clear_buffer_verified(&slot->bh);
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[2],
					      IALLOC_TEST_NGROUPS + 1,
					      &slot->bh);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);

	/* Pre-marked IBITMAP_CORRUPT -> -EFSCORRUPTED */
	set_bit(EXT4_GROUP_INFO_IBITMAP_CORRUPT_BIT,
		&ctx->grp_infos[2]->bb_state);
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[2], 2, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);

#ifdef CONFIG_EXT4_DEBUG
	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(3));
	KUNIT_ASSERT_NOT_NULL(test, slot);
	clear_buffer_verified(&slot->bh);
	ctx->sbi.s_simulate_fail = EXT4_SIM_IBITMAP_CRC;
	err = ext4_validate_inode_bitmap_test(sb, &ctx->gdps[3], 3, &slot->bh);
	KUNIT_EXPECT_EQ(test, err, -EFSBADCRC);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_simulate_fail, 0UL);
#endif
}

/* =========================================================================
 * 3. Reading Inode Bitmaps: ext4_read_inode_bitmap_test
 * =========================================================================
 */

static void ialloc_test_read_ibitmap_disk_and_cache(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *slot;
	struct buffer_head *bh;

	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* Put distinctive valid bitmap on disk and invalidate cache flags */
	ext4_set_bit(5, slot->disk_buf);
	memcpy(slot->cache_buf, slot->disk_buf, IALLOC_TEST_BLK_SIZE);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);
	memset(slot->cache_buf, 0, IALLOC_TEST_BLK_SIZE);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);

	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_bh_calls, 1U);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(5, bh->b_data));
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)buffer_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)buffer_verified(bh));
	brelse(bh);
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), 1);

	/* Second read hits bitmap_uptodate fast path with zero I/O */
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_bh_calls, 1U);
	brelse(bh);

	/* buffer_uptodate && !bitmap_uptodate promotes bitmap_uptodate without I/O */
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_bh_calls, 1U);
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)buffer_verified(bh));
	brelse(bh);
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), 1);
}

static void ialloc_test_read_ibitmap_uninit(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *slot0, *slot1;
	struct buffer_head *bh;
	unsigned int bit;

	/* Group 1 with EXT4_BG_INODE_UNINIT initializes in memory without I/O */
	slot1 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot1);
	memset(slot1->cache_buf, 0xaa, IALLOC_TEST_BLK_SIZE);
	clear_bitmap_uptodate(&slot1->bh);
	clear_buffer_uptodate(&slot1->bh);
	clear_buffer_verified(&slot1->bh);
	ctx->gdps[1].bg_flags |= cpu_to_le16(EXT4_BG_INODE_UNINIT);

	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_bh_calls, 0U);
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)buffer_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)buffer_verified(bh));
	for (bit = 0; bit < IALLOC_TEST_INODES_PER_GRP; bit++)
		KUNIT_EXPECT_FALSE_MSG(test, (bool)ext4_test_bit(bit, bh->b_data),
				       "expected bit %u cleared", bit);
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_test_bit(IALLOC_TEST_INODES_PER_GRP,
					      bh->b_data));
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_test_bit(IALLOC_TEST_BLK_SIZE * 8 - 1,
					      bh->b_data));
	brelse(bh);

	/* Group 0 marked EXT4_BG_INODE_UNINIT is rejected as corrupt */
	slot0 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(0));
	KUNIT_ASSERT_NOT_NULL(test, slot0);
	clear_bitmap_uptodate(&slot0->bh);
	clear_buffer_uptodate(&slot0->bh);
	ctx->gdps[0].bg_flags |= cpu_to_le16(EXT4_BG_INODE_UNINIT);

	bh = ext4_read_inode_bitmap_test(sb, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, atomic_read(&slot0->bh.b_count), 1);
}

static void ialloc_test_read_ibitmap_errors(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *slot;
	struct buffer_head *bh;

	/* NULL group descriptor */
	ctx->fail_gdp_mask = BIT(1);
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	ctx->fail_gdp_mask = 0;

	/* Invalid bitmap_blk <= s_first_data_block (1) */
	ext4_inode_bitmap_set(sb, &ctx->gdps[1], IALLOC_TEST_FIRST_DATA_BLK);
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));

	/* Invalid bitmap_blk >= ext4_blocks_count(es) */
	ctx->grp_infos[1]->bb_state = 0;
	ext4_inode_bitmap_set(sb, &ctx->gdps[1], IALLOC_TEST_TOTAL_BLKS);
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));
	ctx->grp_infos[1]->bb_state = 0;
	ext4_inode_bitmap_set(sb, &ctx->gdps[1], ialloc_test_ibitmap_blk(1));

	/* ext4_sb_getblk allocation failure */
	ctx->fail_getblk = true;
	bh = ext4_read_inode_bitmap_test(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -ENOMEM);
	ctx->fail_getblk = false;

	/* Disk read I/O failure */
	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_uptodate(&slot->bh);
	ctx->fail_read_eio = true;
	bh = ext4_read_inode_bitmap_test(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EIO);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[2]));
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), 1);
	ctx->fail_read_eio = false;

	/* Bad CRC after successful disk read */
	slot = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(3));
	KUNIT_ASSERT_NOT_NULL(test, slot);
	slot->disk_buf[0] ^= 0x80;
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);
	bh = ext4_read_inode_bitmap_test(sb, 3);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSBADCRC);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[3]));
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), 1);
}

/* =========================================================================
 * 4. Freeing Inodes: ext4_free_inode
 * =========================================================================
 */

static void ialloc_test_free_inode_reg_and_dir(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk;
	s64 free_before, dirs_before;

	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk);

	/* Mark bit 2 (ino 67, reg) and bit 3 (ino 68, dir) allocated in group 1 */
	ext4_set_bit(2, iblk->cache_buf);
	ext4_set_bit(3, iblk->cache_buf);
	ext4_free_inodes_set(sb, &ctx->gdps[1], 62);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 1);
	percpu_counter_add(&ctx->sbi.s_freeinodes_counter, -2);
	percpu_counter_add(&ctx->sbi.s_dirs_counter, 1);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);
	ialloc_test_enable_flex_bg(ctx, 1);

	free_before = percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter);
	dirs_before = percpu_counter_sum_positive(&ctx->sbi.s_dirs_counter);

	/* Free regular file ino 67 (group 1, bit 2, flex_group 0) */
	ialloc_test_prep_standalone_free_inode(ctx, 67, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);

	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(2, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[1]), 63U);
	KUNIT_EXPECT_EQ(test, ext4_used_dirs_count(sb, &ctx->gdps[1]), 1U);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter),
			free_before + 1);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_dirs_counter),
			dirs_before);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->flex_grps[0].free_inodes),
			(int)(IALLOC_TEST_BG0_FREE_INODES + 63));
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->flex_grps[0].used_dirs), 2);
	KUNIT_EXPECT_TRUE(test, iblk->marked_dirty);
	KUNIT_EXPECT_TRUE(test, ctx->gdesc_marked_dirty);
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_inode_bitmap_csum_verify(sb, &ctx->gdps[1],
							      &iblk->bh));
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_group_desc_csum_verify(sb, 1,
							    &ctx->gdps[1]));

	/* Free directory ino 68 (group 1, bit 3, flex_group 0) */
	ialloc_test_prep_standalone_free_inode(ctx, 68, S_IFDIR | 0755);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);

	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(3, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[1]), 64U);
	KUNIT_EXPECT_EQ(test, ext4_used_dirs_count(sb, &ctx->gdps[1]), 0U);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter),
			free_before + 2);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_dirs_counter),
			dirs_before - 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->flex_grps[0].used_dirs), 1);
}

static void ialloc_test_free_inode_guards(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk0, *iblk1;
	struct inode *inode = &ctx->standalone_ei.vfs_inode;

	iblk0 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(0));
	iblk1 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk0);
	KUNIT_ASSERT_NOT_NULL(test, iblk1);

	ext4_set_bit(0, iblk1->cache_buf); /* ino 65 */
	ext4_free_inodes_set(sb, &ctx->gdps[1], 63);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);

	/* NULL i_sb is ignored */
	ialloc_test_prep_standalone_free_inode(ctx, 65, S_IFREG | 0644);
	inode->i_sb = NULL;
	ext4_free_inode(NULL, inode);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(0, iblk1->cache_buf));

	/* i_count > 1 is rejected before clear_inode */
	ialloc_test_prep_standalone_free_inode(ctx, 65, S_IFREG | 0644);
	atomic_set(&inode->i_count, 2);
	ext4_free_inode(NULL, inode);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(0, iblk1->cache_buf));
	KUNIT_EXPECT_FALSE(test, !!(inode_state_read_once(inode) & I_CLEAR));

	/* i_nlink > 0 is rejected before clear_inode */
	ialloc_test_prep_standalone_free_inode(ctx, 65, S_IFREG | 0644);
	set_nlink(inode, 1);
	ext4_free_inode(NULL, inode);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(0, iblk1->cache_buf));
	KUNIT_EXPECT_FALSE(test, !!(inode_state_read_once(inode) & I_CLEAR));

	/* Reserved ino < EXT4_FIRST_INO(sb) clears VFS inode then errors out */
	ialloc_test_prep_standalone_free_inode(ctx, 5, S_IFREG | 0644);
	ext4_free_inode(NULL, inode);
	KUNIT_EXPECT_TRUE(test, !!(inode_state_read_once(inode) & I_CLEAR));
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(4, iblk0->cache_buf));

	/* Out-of-range ino > s_inodes_count clears VFS inode then errors out */
	ialloc_test_prep_standalone_free_inode(ctx, IALLOC_TEST_TOTAL_INODES + 1,
					       S_IFREG | 0644);
	ext4_free_inode(NULL, inode);
	KUNIT_EXPECT_TRUE(test, !!(inode_state_read_once(inode) & I_CLEAR));
}

static void ialloc_test_free_inode_corrupt_and_journal_errors(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk1, *iblk2, *iblk3;

	/* 1. Double-free (bit already clear) marks group bitmap corrupt */
	iblk1 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk1);
	ialloc_test_prep_standalone_free_inode(ctx, 66, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[1]), 64U);

	/* 2. Pre-corrupt group skips bitmap clear unless EXT4_FC_REPLAY */
	iblk2 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, iblk2);
	ext4_set_bit(0, iblk2->cache_buf); /* ino 129 */
	ext4_free_inodes_set(sb, &ctx->gdps[2], 63);
	ialloc_test_sync_ibitmap_csum(ctx, 2, true);
	set_bit(EXT4_GROUP_INFO_IBITMAP_CORRUPT_BIT,
		&ctx->grp_infos[2]->bb_state);

	ialloc_test_prep_standalone_free_inode(ctx, 129, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(0, iblk2->cache_buf));

	ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;
	ialloc_test_prep_standalone_free_inode(ctx, 129, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);
	ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(0, iblk2->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[2]), 64U);

	/* 3. Write-access failure on bitmap_bh (1st) vs bh2 (2nd) */
	iblk3 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(3));
	KUNIT_ASSERT_NOT_NULL(test, iblk3);
	ext4_set_bit(0, iblk3->cache_buf); /* ino 193 */
	ext4_free_inodes_set(sb, &ctx->gdps[3], 63);
	ialloc_test_sync_ibitmap_csum(ctx, 3, true);

	ctx->write_access_calls = 0;
	ctx->fail_write_access_err = -EIO;
	ctx->fail_write_access_on_call = 1;
	ialloc_test_prep_standalone_free_inode(ctx, 193, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(0, iblk3->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[3]), 63U);

	ctx->write_access_calls = 0;
	ctx->fail_write_access_on_call = 2;
	iblk3->marked_dirty = false;
	ialloc_test_prep_standalone_free_inode(ctx, 193, S_IFREG | 0644);
	ext4_free_inode(NULL, &ctx->standalone_ei.vfs_inode);
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(0, iblk3->cache_buf));
	KUNIT_EXPECT_TRUE(test, iblk3->marked_dirty);
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[3]), 63U);
	ctx->fail_write_access_err = 0;
	ctx->fail_write_access_on_call = 0;
}

/* =========================================================================
 * 5. Orlov Directory Allocator: ext4_find_group_orlov_test
 * =========================================================================
 */

static void ialloc_test_orlov_topdir_spread(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct qstr qname = QSTR_INIT("projects", 8);
	struct inode *subdir;
	ext4_group_t grp = 0;
	int ret;

	/*
	 * Configure group 2 as the unique above-average group with the lowest
	 * directory count (0 used_dirs, 64 free_inodes, 900 free_clusters).
	 */
	ext4_used_dirs_set(sb, &ctx->gdps[0], 5);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 4);
	ext4_used_dirs_set(sb, &ctx->gdps[2], 0);
	ext4_used_dirs_set(sb, &ctx->gdps[3], 6);
	ext4_free_group_clusters_set(sb, &ctx->gdps[2], 900);

	ret = ext4_find_group_orlov_test(sb, ctx->root_inode, &grp,
					 S_IFDIR | 0755, &qname);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);

	/* qstr == NULL uses random start, still converges on best group 2 */
	grp = 0;
	ret = ext4_find_group_orlov_test(sb, ctx->root_inode, &grp,
					 S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);

	/* Non-root directory with EXT4_INODE_TOPDIR also uses topdir spread */
	subdir = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, subdir);
	subdir->i_ino = 65;
	subdir->i_mode = S_IFDIR | 0755;
	EXT4_I(subdir)->i_block_group = 1;
	ext4_set_inode_flag(subdir, EXT4_INODE_TOPDIR);
	ialloc_test_track_inode(test, ctx, subdir);

	grp = 0;
	ret = ext4_find_group_orlov_test(sb, subdir, &grp, S_IFDIR | 0755,
					 &qname);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);
}

static void ialloc_test_orlov_subdir_and_thresholds(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *parent;
	ext4_group_t grp = 0;
	int ret;

	parent = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	parent->i_ino = 65;
	parent->i_mode = S_IFDIR | 0755;
	EXT4_I(parent)->i_block_group = 1;
	ialloc_test_track_inode(test, ctx, parent);

	/* Healthy parent_group (1) is preferred */
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 1U);

	/* i_last_alloc_group overrides parent_group */
	EXT4_I(parent)->i_last_alloc_group = 3;
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);
	EXT4_I(parent)->i_last_alloc_group = ~0U;

	/* Group 1 exceeds max_dirs -> advances cyclically to group 2 */
	ext4_used_dirs_set(sb, &ctx->gdps[1], 40);
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 0);

	/* Group 1 below min_inodes -> advances to group 2 */
	ext4_free_inodes_set(sb, &ctx->gdps[1], 2);
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);
	ext4_free_inodes_set(sb, &ctx->gdps[1], IALLOC_TEST_INODES_PER_GRP);

	/* Group 1 below min_clusters -> advances to group 2 */
	ext4_free_group_clusters_set(sb, &ctx->gdps[1], 10);
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);
}

static void ialloc_test_orlov_flex_bg_and_fallback(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *parent;
	ext4_group_t grp = 0;
	ext4_group_t g;
	int ret;

	parent = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	parent->i_ino = 12;
	parent->i_mode = S_IFDIR | 0755;
	EXT4_I(parent)->i_block_group = 0;
	ialloc_test_track_inode(test, ctx, parent);

	/*
	 * Flex size 2: flex 0 = {0, 1} has excessive dirs; flex 1 = {2, 3}
	 * is healthy, with group 2 full (0 free inodes) and group 3 having 64
	 * free inodes -> packs into first available constituent group 3.
	 */
	ext4_used_dirs_set(sb, &ctx->gdps[0], 30);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 30);
	ext4_free_inodes_set(sb, &ctx->gdps[2], 0);
	ext4_free_inodes_set(sb, &ctx->gdps[3], 64);
	percpu_counter_set(&ctx->sbi.s_dirs_counter, 60);
	percpu_counter_set(&ctx->sbi.s_freeinodes_counter,
			   IALLOC_TEST_BG0_FREE_INODES + 64 + 0 + 64);
	ialloc_test_enable_flex_bg(ctx, 1);

	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);

	/* Disable flex_bg and test fallback_retry with inflated counter */
	ctx->sbi.s_log_groups_per_flex = 0;
	ext4_clear_feature_flex_bg(sb);
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++) {
		ext4_used_dirs_set(sb, &ctx->gdps[g], 50);
		ext4_free_inodes_set(sb, &ctx->gdps[g], (g == 3) ? 1U : 0U);
	}
	/* Counter claims 200 free inodes (avefreei = 50), only bg 3 has 1 */
	percpu_counter_set(&ctx->sbi.s_freeinodes_counter, 200);

	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);

	/* All groups completely full -> returns -1 */
	ext4_free_inodes_set(sb, &ctx->gdps[3], 0);
	ret = ext4_find_group_orlov_test(sb, parent, &grp, S_IFDIR | 0755, NULL);
	KUNIT_EXPECT_EQ(test, ret, -1);
}

/* =========================================================================
 * 6. Non-Directory Group Allocator: ext4_find_group_other_test
 * =========================================================================
 */

static void ialloc_test_find_group_other_non_flex(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *parent;
	ext4_group_t grp = 0;
	ext4_group_t g;
	int ret;

	parent = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	parent->i_ino = 65;
	parent->i_mode = S_IFDIR | 0755;
	EXT4_I(parent)->i_block_group = 1;
	ialloc_test_track_inode(test, ctx, parent);

	/* 1. Parent group 1 has both free inodes and free clusters */
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 1U);

	/*
	 * 2. Quadratic hash probe when parent group 1 has 0 free inodes:
	 *    start = (1 + 65) % 4 = 2; step i = 1 -> (2 + 1) % 4 = 3.
	 */
	ext4_free_inodes_set(sb, &ctx->gdps[1], 0);
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);

	/*
	 * 3. Linear fallback when no group has free clusters:
	 *    starts at parent_group = 1, ++*group checks 2 first.
	 */
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++)
		ext4_free_group_clusters_set(sb, &ctx->gdps[g], 0);
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 2U);

	/* 4. Returns -1 when all groups have 0 free inodes */
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++)
		ext4_free_inodes_set(sb, &ctx->gdps[g], 0);
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, -1);
}

static void ialloc_test_find_group_other_flex_bg(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *parent;
	ext4_group_t grp = 0;
	int ret;

	parent = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	parent->i_ino = 65;
	parent->i_mode = S_IFDIR | 0755;
	EXT4_I(parent)->i_block_group = 1;
	ialloc_test_track_inode(test, ctx, parent);

	/*
	 * Flex size 2: parent (i_ino = 65) in bg 1 (flex 0 = {0, 1}) spreads
	 * by parent->i_ino % flex_size -> bg 1 first, then falls back to bg 0.
	 */
	ialloc_test_enable_flex_bg(ctx, 1);
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 1U);

	ext4_free_inodes_set(sb, &ctx->gdps[1], 0);
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 0U);

	/* Flex 0 full, i_last_alloc_group = 2 (flex 1 = {2, 3}) retries flex 1 -> bg 3 */
	ext4_free_inodes_set(sb, &ctx->gdps[0], 0);
	ialloc_test_enable_flex_bg(ctx, 1);
	EXT4_I(parent)->i_last_alloc_group = 2;
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);

	/* Flex 0 full and i_last_alloc_group unset -> Orlov fallback picks flex 1 -> bg 3 */
	EXT4_I(parent)->i_last_alloc_group = ~0U;
	ret = ext4_find_group_other_test(sb, parent, &grp, S_IFREG | 0644);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp, 3U);
}

/* =========================================================================
 * 7. Recently Deleted & Bit Search: recently_deleted & find_inode_bit
 * =========================================================================
 */

static void ialloc_test_recently_deleted(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *it_slot;
	struct ext4_inode *raw;
	u32 now;

	/* 1. EXT4_BG_INODE_UNINIT returns 0 immediately */
	ctx->gdps[1].bg_flags |= cpu_to_le16(EXT4_BG_INODE_UNINIT);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 0), 0);
	ctx->gdps[1].bg_flags &= ~cpu_to_le16(EXT4_BG_INODE_UNINIT);

	/* 2. Inode >= (64 - itable_unused) was never used -> returns 0 */
	ext4_itable_unused_set(sb, &ctx->gdps[1], 54); /* 0..9 used */
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 10), 0);

	/* 3. Uncached inode table block -> returns 0 */
	it_slot = ialloc_test_setup_blk(ctx, ialloc_test_itable_blk(1),
					false, true);
	KUNIT_ASSERT_NOT_NULL(test, it_slot);
	raw = (struct ext4_inode *)(it_slot->cache_buf +
				    2 * IALLOC_TEST_INODE_SIZE);
	now = ktime_get_real_seconds();
	raw->i_dtime = cpu_to_le32(now - 10);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 0);

	/* 4. Cached but !buffer_uptodate -> returns 0 */
	it_slot->in_cache = true;
	clear_buffer_uptodate(&it_slot->bh);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 0);
	set_buffer_uptodate(&it_slot->bh);

	/* 5. dtime == 0 -> returns 0 */
	raw->i_dtime = 0;
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 0);

	/* 6. Clean buffer uses 60s window; dirty buffer extends to 360s */
	now = ktime_get_real_seconds();
	raw->i_dtime = cpu_to_le32(now - 20);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 1);

	raw->i_dtime = cpu_to_le32(now - 120);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 0);

	set_buffer_dirty(&it_slot->bh);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 1);

	raw->i_dtime = cpu_to_le32(now - 400);
	KUNIT_EXPECT_EQ(test, ext4_recently_deleted_test(sb, 1, 2), 0);
	clear_buffer_dirty(&it_slot->bh);
}

static void ialloc_test_find_inode_bit(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk, *it_slot;
	struct ext4_inode *raw1;
	unsigned long ino;
	unsigned int bit;
	int ret;

	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk);
	ext4_itable_unused_set(sb, &ctx->gdps[1], 0);

	/* Mark all bits except bit 1 (recently deleted) and bit 6 (old) */
	for (bit = 0; bit < IALLOC_TEST_INODES_PER_GRP; bit++) {
		if (bit != 1 && bit != 6)
			ext4_set_bit(bit, iblk->cache_buf);
	}
	it_slot = ialloc_test_setup_blk(ctx, ialloc_test_itable_blk(1),
					true, true);
	KUNIT_ASSERT_NOT_NULL(test, it_slot);
	raw1 = (struct ext4_inode *)(it_slot->cache_buf + IALLOC_TEST_INODE_SIZE);
	raw1->i_dtime = cpu_to_le32((u32)ktime_get_real_seconds() - 10);

	/* No-journal mode skips recently deleted bit 1 in favor of bit 6 */
	ino = 0;
	ret = ext4_find_inode_bit_test(sb, 1, &iblk->bh, &ino);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ino, 6UL);

	/* Journaled mode ignores recently_deleted and picks bit 1 directly */
	ctx->sbi.s_journal = (journal_t *)1UL;
	ino = 0;
	ret = ext4_find_inode_bit_test(sb, 1, &iblk->bh, &ino);
	ctx->sbi.s_journal = NULL;
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ino, 1UL);

	/* Fallback: when bit 6 is also allocated, returns recently deleted bit 1 */
	ext4_set_bit(6, iblk->cache_buf);
	ino = 0;
	ret = ext4_find_inode_bit_test(sb, 1, &iblk->bh, &ino);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ino, 1UL);

	/* Full bitmap returns 0 */
	ext4_set_bit(1, iblk->cache_buf);
	ino = 0;
	ret = ext4_find_inode_bit_test(sb, 1, &iblk->bh, &ino);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/* =========================================================================
 * 8. Marking Specific Inodes Used: ext4_mark_inode_used
 * =========================================================================
 */

static void ialloc_test_mark_inode_used_uninit(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk, *bblk;
	int err;

	/*
	 * Configure group 2 with both EXT4_BG_INODE_UNINIT and
	 * EXT4_BG_BLOCK_UNINIT.  Group 2 has no backup superblock, so overhead
	 * is 1 (bbitmap) + 1 (ibitmap) + 16 (itable) = 18 clusters -> 1006 free.
	 */
	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(2));
	bblk = ialloc_test_find_blk(ctx, ialloc_test_bbitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, iblk);
	KUNIT_ASSERT_NOT_NULL(test, bblk);

	clear_bitmap_uptodate(&iblk->bh);
	clear_buffer_uptodate(&iblk->bh);
	clear_buffer_verified(&iblk->bh);
	ctx->gdps[2].bg_flags = cpu_to_le16(EXT4_BG_INODE_UNINIT |
					    EXT4_BG_BLOCK_UNINIT);
	ext4_free_group_clusters_set(sb, &ctx->gdps[2], 0);
	ext4_itable_unused_set(sb, &ctx->gdps[2], 64);
	ext4_free_inodes_set(sb, &ctx->gdps[2], 64);
	ext4_group_desc_csum_set(sb, 2, &ctx->gdps[2]);

	/* Inode 135 is in group 2, bit 6 ((135 - 1) % 64 == 6) */
	err = ext4_mark_inode_used(sb, 135);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test,
			le16_to_cpu(ctx->gdps[2].bg_flags) &
			(EXT4_BG_INODE_UNINIT | EXT4_BG_BLOCK_UNINIT), 0);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, &ctx->gdps[2]),
			1006U);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(6, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_itable_unused_count(sb, &ctx->gdps[2]), 57U);
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[2]), 63U);
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_block_bitmap_csum_verify(sb, &ctx->gdps[2],
							      &bblk->bh));
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_inode_bitmap_csum_verify(sb, &ctx->gdps[2],
							      &iblk->bh));
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_group_desc_csum_verify(sb, 2,
							    &ctx->gdps[2]));

	/* Idempotent when bit 6 is already set */
	err = ext4_mark_inode_used(sb, 135);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[2]), 63U);

	/* Lower bit 2 (ino 131) decrements free_inodes without raising itable_unused */
	err = ext4_mark_inode_used(sb, 131);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(2, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_itable_unused_count(sb, &ctx->gdps[2]), 57U);
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[2]), 62U);
}

static void ialloc_test_mark_inode_used_errors(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk;

	/* Bounds checks: below EXT4_FIRST_INO and above max_ino */
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 10), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test,
			ext4_mark_inode_used(sb, IALLOC_TEST_TOTAL_INODES + 1),
			-EFSCORRUPTED);

	/* Inode bitmap read I/O failure */
	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk);
	clear_bitmap_uptodate(&iblk->bh);
	clear_buffer_uptodate(&iblk->bh);
	ctx->fail_read_eio = true;
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 65), -EIO);
	ctx->fail_read_eio = false;

	/* Dirty metadata error on group 2 (ino 129) */
	ctx->fail_dirty_metadata_err = -EIO;
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 129), -EIO);
	ctx->fail_dirty_metadata_err = 0;

	/* sync_dirty_buffer I/O error on unmapped dirty buffer (ino 130) */
	ctx->fail_sync_dirty_buffer = true;
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 130), -EIO);
	ctx->fail_sync_dirty_buffer = false;

	/* Block bitmap read and wait failures during EXT4_BG_BLOCK_UNINIT init */
	ctx->gdps[3].bg_flags |= cpu_to_le16(EXT4_BG_BLOCK_UNINIT);
	ext4_group_desc_csum_set(sb, 3, &ctx->gdps[3]);
	ctx->fail_bbitmap_err = -EIO;
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 193), -EIO);
	ctx->fail_bbitmap_err = 0;

	ctx->fail_wait_bbitmap_err = -EFSCORRUPTED;
	KUNIT_EXPECT_EQ(test, ext4_mark_inode_used(sb, 194), -EFSCORRUPTED);
	ctx->fail_wait_bbitmap_err = 0;
}

/* =========================================================================
 * 9. Xattr Credits & Inode Allocation: __ext4_new_inode
 * =========================================================================
 */

#ifdef CONFIG_EXT4_FS_POSIX_ACL
static struct posix_acl *ialloc_test_err_get_inode_acl(struct inode *inode,
						       int type, bool rcu)
{
	return ERR_PTR(-EIO);
}

static const struct inode_operations ialloc_test_err_acl_iops = {
	.get_inode_acl = ialloc_test_err_get_inode_acl,
};
#endif

static void ialloc_test_xattr_credits_for_new_inode(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	int base, enc_creds, base_ea, enc_ea_creds;

	base = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
						     S_IFREG | 0644, false);
	KUNIT_ASSERT_GE(test, base, 0);
	if (!IS_ENABLED(CONFIG_SECURITY))
		KUNIT_EXPECT_EQ(test, base, 0);

	/* Without ea_inode, encryption xattr adds 7 credits */
	enc_creds = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
							  S_IFREG | 0644, true);
	KUNIT_EXPECT_EQ(test, enc_creds, base + 7);

	/* With ea_inode on 1024B blocks, 40B fscrypt context adds 17 credits */
	ext4_set_feature_ea_inode(sb);
	base_ea = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
							S_IFREG | 0644, false);
	enc_ea_creds = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
							     S_IFREG | 0644,
							     true);
	KUNIT_EXPECT_EQ(test, enc_ea_creds, base_ea + 17);
	ext4_clear_feature_ea_inode(sb);

#ifdef CONFIG_EXT4_FS_POSIX_ACL
	{
		struct posix_acl *acl;
		const struct inode_operations *orig_iops;
		int reg_acl, dir_acl, err_acl;

		acl = posix_acl_alloc(1, GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, acl);
		set_cached_acl(ctx->root_inode, ACL_TYPE_DEFAULT, acl);
		posix_acl_release(acl);

		reg_acl = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
								S_IFREG | 0644,
								false);
		dir_acl = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
								S_IFDIR | 0755,
								false);
		KUNIT_EXPECT_EQ(test, reg_acl, base + 7);
		KUNIT_EXPECT_EQ(test, dir_acl, base + 14);
		forget_cached_acl(ctx->root_inode, ACL_TYPE_DEFAULT);

		orig_iops = ctx->root_inode->i_op;
		ctx->root_inode->i_op = &ialloc_test_err_acl_iops;
		sb->s_flags |= SB_POSIXACL;
		err_acl = ext4_xattr_credits_for_new_inode_test(ctx->root_inode,
								S_IFREG | 0644,
								false);
		sb->s_flags &= ~SB_POSIXACL;
		ctx->root_inode->i_op = orig_iops;
		KUNIT_EXPECT_EQ(test, err_acl, -EIO);
	}
#endif
}

static void ialloc_test_new_inode_types_and_flags(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	handle_t *nojournal;
	struct ialloc_test_blk *iblk0;
	struct inode *reg, *dir, *lnk, *fifo, *ea_ino;
	__le32 inum_le, gen_le;
	u32 expected_seed;
	s64 free_after_reg;

	nojournal = ext4_journal_start_sb(sb, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(nojournal));

	ext4_set_feature_inline_data(sb);
	ialloc_test_enable_flex_bg(ctx, 1);
	EXT4_I(ctx->root_inode)->i_flags = EXT4_SYNC_FL | EXT4_NODUMP_FL |
					   EXT4_DIRSYNC_FL | EXT4_TOPDIR_FL |
					   EXT4_EXTENTS_FL;

	/* 1. Regular file in group 0 (first free bit is 10 -> ino 11) */
	reg = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644,
			     NULL, 0, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(reg));
	ialloc_test_track_inode(test, ctx, reg);
	KUNIT_EXPECT_EQ(test, reg->i_ino, 11UL);
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(reg, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(reg)->eh_magic),
			le16_to_cpu(EXT4_EXT_MAGIC));
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_state(reg, EXT4_STATE_NEW));
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(reg, EXT4_STATE_MAY_INLINE_DATA));
	KUNIT_EXPECT_EQ(test,
			EXT4_I(reg)->i_flags &
			(EXT4_SYNC_FL | EXT4_NODUMP_FL | EXT4_DIRSYNC_FL |
			 EXT4_TOPDIR_FL),
			(u32)(EXT4_SYNC_FL | EXT4_NODUMP_FL));
	KUNIT_EXPECT_EQ(test, EXT4_I(reg)->i_extra_isize,
			(u16)IALLOC_TEST_EXTRA_ISIZE);

	inum_le = cpu_to_le32(reg->i_ino);
	gen_le = cpu_to_le32(reg->i_generation);
	expected_seed = ext4_chksum(ctx->sbi.s_csum_seed, (u8 *)&inum_le,
				    sizeof(inum_le));
	expected_seed = ext4_chksum(expected_seed, (u8 *)&gen_le,
				    sizeof(gen_le));
	KUNIT_EXPECT_EQ(test, EXT4_I(reg)->i_csum_seed, expected_seed);

	/*
	 * 2. Directory inherits DIRSYNC (unlike regular files), still strips
	 *    non-inheritable TOPDIR, and increments directory counters.
	 */
	dir = ext4_new_inode(nojournal, ctx->root_inode, S_IFDIR | 0755,
			     NULL, 12, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(dir));
	ialloc_test_track_inode(test, ctx, dir);
	KUNIT_EXPECT_EQ(test, dir->i_ino, 12UL);
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(dir, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_EQ(test,
			EXT4_I(dir)->i_flags &
			(EXT4_SYNC_FL | EXT4_NODUMP_FL | EXT4_DIRSYNC_FL |
			 EXT4_TOPDIR_FL),
			(u32)(EXT4_SYNC_FL | EXT4_NODUMP_FL |
			      EXT4_DIRSYNC_FL));
	KUNIT_EXPECT_EQ(test, ext4_used_dirs_count(sb, &ctx->gdps[0]), 2U);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->flex_grps[0].used_dirs), 2);

	/* 3. Symlink gets EXT4_INODE_EXTENTS; FIFO does not */
	lnk = ext4_new_inode(nojournal, ctx->root_inode, S_IFLNK | 0777,
			     NULL, 13, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(lnk));
	ialloc_test_track_inode(test, ctx, lnk);
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(lnk, EXT4_INODE_EXTENTS));

	fifo = ext4_new_inode(nojournal, ctx->root_inode, S_IFIFO | 0600,
			      NULL, 14, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(fifo));
	ialloc_test_track_inode(test, ctx, fifo);
	KUNIT_EXPECT_FALSE(test, ext4_test_inode_flag(fifo, EXT4_INODE_EXTENTS));

	/* 4. EXT4_EA_INODE_FL suppresses EXT4_STATE_MAY_INLINE_DATA */
	ea_ino = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0600,
				NULL, 15, NULL, EXT4_EA_INODE_FL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ea_ino));
	ialloc_test_track_inode(test, ctx, ea_ino);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(ea_ino,
						 EXT4_STATE_MAY_INLINE_DATA));

	/* 5. Full lifecycle round-trip: unlink + iput frees ino 11 back */
	iblk0 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(0));
	KUNIT_ASSERT_NOT_NULL(test, iblk0);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(10, iblk0->cache_buf));
	free_after_reg = percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter);

	ialloc_test_untrack_inode(ctx, reg);
	clear_nlink(reg);
	iput(reg);

	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(10, iblk0->cache_buf));
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum_positive(&ctx->sbi.s_freeinodes_counter),
			free_after_reg + 1);

	ext4_journal_stop(nojournal);
}

static void ialloc_test_new_inode_ownership_and_projid(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	handle_t *nojournal;
	uid_t explicit_owner[2] = { 1001, 2002 };
	struct inode *ino_own, *ino_grpid, *ino_sgid;

	nojournal = ext4_journal_start_sb(sb, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(nojournal));

	i_gid_write(ctx->root_inode, 3003);
	ext4_set_feature_project(sb);
	EXT4_I(ctx->root_inode)->i_projid = make_kprojid(&init_user_ns, 42);
	ext4_set_inode_flag(ctx->root_inode, EXT4_INODE_PROJINHERIT);

	/* Explicit owner overrides uid/gid and inherits projid 42 */
	ino_own = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0640,
				 NULL, 0, explicit_owner, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_own));
	ialloc_test_track_inode(test, ctx, ino_own);
	KUNIT_EXPECT_EQ(test, i_uid_read(ino_own), 1001U);
	KUNIT_EXPECT_EQ(test, i_gid_read(ino_own), 2002U);
	KUNIT_EXPECT_EQ(test,
			from_kprojid(&init_user_ns, EXT4_I(ino_own)->i_projid),
			42U);

	/* Without PROJINHERIT on parent, child gets EXT4_DEF_PROJID (0) and GRPID gid */
	ext4_clear_inode_flag(ctx->root_inode, EXT4_INODE_PROJINHERIT);
	set_opt(sb, GRPID);
	ino_grpid = ext4_new_inode(nojournal, ctx->root_inode, S_IFDIR | 0750,
				   NULL, 0, NULL, 0);
	clear_opt(sb, GRPID);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_grpid));
	ialloc_test_track_inode(test, ctx, ino_grpid);
	KUNIT_EXPECT_EQ(test, i_gid_read(ino_grpid), 3003U);
	KUNIT_EXPECT_FALSE(test, !!(ino_grpid->i_mode & S_ISGID));
	KUNIT_EXPECT_EQ(test,
			from_kprojid(&init_user_ns, EXT4_I(ino_grpid)->i_projid),
			0U);

	/* Standard inode_init_owner propagates parent's S_ISGID to child dir */
	ctx->root_inode->i_mode |= S_ISGID;
	ino_sgid = ext4_new_inode(nojournal, ctx->root_inode, S_IFDIR | 0755,
				  NULL, 0, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_sgid));
	ialloc_test_track_inode(test, ctx, ino_sgid);
	KUNIT_EXPECT_EQ(test, i_gid_read(ino_sgid), 3003U);
	KUNIT_EXPECT_TRUE(test, !!(ino_sgid->i_mode & S_ISGID));

	ext4_journal_stop(nojournal);
}

static void ialloc_test_new_inode_goal_uninit_and_corrupt_skip(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	handle_t *nojournal;
	struct ialloc_test_blk *iblk0, *iblk2;
	struct inode *ino_goal, *ino_sb_goal, *ino_skip, *ino_started;
	struct inode *ino_replay;

	nojournal = ext4_journal_start_sb(sb, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(nojournal));

	/* 1. Explicit goal = 140 in uninitialized group 2 (bit 11) */
	iblk2 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, iblk2);
	clear_bitmap_uptodate(&iblk2->bh);
	clear_buffer_uptodate(&iblk2->bh);
	clear_buffer_verified(&iblk2->bh);
	ctx->gdps[2].bg_flags = cpu_to_le16(EXT4_BG_INODE_UNINIT |
					    EXT4_BG_BLOCK_UNINIT);
	ext4_group_desc_csum_set(sb, 2, &ctx->gdps[2]);

	ino_goal = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644,
				  NULL, 140, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_goal));
	ialloc_test_track_inode(test, ctx, ino_goal);
	KUNIT_EXPECT_EQ(test, ino_goal->i_ino, 140UL);
	KUNIT_EXPECT_EQ(test,
			le16_to_cpu(ctx->gdps[2].bg_flags) &
			(EXT4_BG_INODE_UNINIT | EXT4_BG_BLOCK_UNINIT), 0);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, &ctx->gdps[2]),
			1006U);
	KUNIT_EXPECT_EQ(test, ext4_itable_unused_count(sb, &ctx->gdps[2]), 52U);

	/* 2. sbi->s_inode_goal = 200 (group 3, bit 7) used when goal == 0 */
	ctx->sbi.s_inode_goal = 200;
	ino_sb_goal = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644,
				     NULL, 0, NULL, 0);
	ctx->sbi.s_inode_goal = 0;
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_sb_goal));
	ialloc_test_track_inode(test, ctx, ino_sb_goal);
	KUNIT_EXPECT_EQ(test, ino_sb_goal->i_ino, 200UL);

	/*
	 * 3. Reserved bit 3 (ino 4 < EXT4_FIRST_INO) cleared in group 0:
	 *    marks group 0 IBITMAP_CORRUPT and advances to group 1 (ino 65).
	 */
	iblk0 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(0));
	KUNIT_ASSERT_NOT_NULL(test, iblk0);
	ext4_clear_bit(3, iblk0->cache_buf);
	ialloc_test_sync_ibitmap_csum(ctx, 0, true);

	ino_skip = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644,
				  NULL, 4, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_skip));
	ialloc_test_track_inode(test, ctx, ino_skip);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[0]));
	KUNIT_EXPECT_EQ(test, ino_skip->i_ino, 65UL);

	ext4_journal_stop(nojournal);

	/* 4. ext4_new_inode_start_handle (handle == NULL) starts handle */
	KUNIT_ASSERT_NULL(test, ext4_journal_current_handle());
	ino_started = ext4_new_inode_start_handle(&nop_mnt_idmap,
						  ctx->root_inode,
						  S_IFREG | 0644, NULL, 66,
						  NULL, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_started));
	ialloc_test_track_inode(test, ctx, ino_started);
	KUNIT_EXPECT_EQ(test, ino_started->i_ino, 66UL);
	KUNIT_ASSERT_NOT_NULL(test, ext4_journal_current_handle());
	ext4_journal_stop(ext4_journal_current_handle());
	KUNIT_EXPECT_NULL(test, ext4_journal_current_handle());

	/*
	 * 5. EXT4_FC_REPLAY with handle == NULL allocates exact goal ino 15 in
	 *    group 0 despite IBITMAP_CORRUPT and without starting a handle.
	 */
	ext4_set_bit(3, iblk0->cache_buf);
	ialloc_test_sync_ibitmap_csum(ctx, 0, true);
	ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;
	ino_replay = ext4_new_inode(NULL, ctx->root_inode, S_IFREG | 0644,
				    NULL, 15, NULL, 0);
	ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;
	KUNIT_ASSERT_FALSE(test, IS_ERR(ino_replay));
	ialloc_test_track_inode(test, ctx, ino_replay);
	KUNIT_EXPECT_EQ(test, ino_replay->i_ino, 15UL);
	KUNIT_EXPECT_NULL(test, ext4_journal_current_handle());
}

static void ialloc_test_new_inode_errors(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	handle_t *nojournal;
	struct inode *unlinked_dir, *dup_existing, *res;
	struct ialloc_test_blk *iblk;
	ext4_group_t g;

	nojournal = ext4_journal_start_sb(sb, EXT4_HT_INODE, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(nojournal));

	/* 1. NULL dir or unlinked dir -> -EPERM */
	res = ext4_new_inode(nojournal, NULL, S_IFREG | 0644, NULL, 0, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EPERM);

	unlinked_dir = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, unlinked_dir);
	unlinked_dir->i_ino = 50;
	unlinked_dir->i_mode = S_IFDIR | 0755;
	clear_nlink(unlinked_dir);
	ialloc_test_track_inode(test, ctx, unlinked_dir);
	res = ext4_new_inode(nojournal, unlinked_dir, S_IFREG | 0644, NULL, 0,
			     NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EPERM);

	/* 2. Emergency shutdown (-EIO) and emergency RO (-EROFS) */
	set_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     0, NULL, 0);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);

	set_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     0, NULL, 0);
	clear_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EROFS);

	/* 3. VFS alloc_inode failure -> -ENOMEM */
	ctx->fail_alloc_inode = true;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     0, NULL, 0);
	ctx->fail_alloc_inode = false;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -ENOMEM);

	/* 4. Journal write-access and dirty-metadata failures in group 1 */
	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk);

	ctx->write_access_calls = 0;
	ctx->fail_write_access_err = -EIO;
	ctx->fail_write_access_on_call = 1;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     70, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(5, iblk->cache_buf));

	ctx->write_access_calls = 0;
	ctx->fail_write_access_on_call = 2;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     71, NULL, 0);
	ctx->fail_write_access_err = 0;
	ctx->fail_write_access_on_call = 0;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(6, iblk->cache_buf));
	ext4_clear_bit(6, iblk->cache_buf);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);

	ctx->dirty_metadata_calls = 0;
	ctx->fail_dirty_metadata_err = -EIO;
	ctx->fail_dirty_metadata_on_call = 1;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     71, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	ext4_clear_bit(6, iblk->cache_buf);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);

	ctx->dirty_metadata_calls = 0;
	ctx->fail_dirty_metadata_on_call = 2;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     71, NULL, 0);
	ctx->fail_dirty_metadata_err = 0;
	ctx->fail_dirty_metadata_on_call = 0;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	ext4_clear_bit(6, iblk->cache_buf);
	ext4_free_inodes_set(sb, &ctx->gdps[1], 64);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);

#ifdef CONFIG_EXT4_FS_SECURITY
	/* 5. ext4_init_security failure rolls back allocated inode bit */
	ctx->fail_init_security_err = -EACCES;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     70, NULL, 0);
	ctx->fail_init_security_err = 0;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EACCES);
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(5, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[1]), 64U);
#endif

	/* 6. ext4_mark_inode_dirty failure rolls back allocated inode bit */
	ctx->fail_mark_inode_dirty_err = -EIO;
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     70, NULL, 0);
	ctx->fail_mark_inode_dirty_err = 0;
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(5, iblk->cache_buf));
	KUNIT_EXPECT_EQ(test, ext4_free_inodes_count(sb, &ctx->gdps[1]), 64U);

	/*
	 * 7. Doubly-allocated inode (insert_inode_locked collision):
	 *    Pre-hash ino 140 in VFS cache, leave only bg 2 non-empty with bit 11
	 *    clear -> marks bg 2 IBITMAP_CORRUPT and returns -EIO.
	 */
	dup_existing = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, dup_existing);
	dup_existing->i_ino = 140;
	dup_existing->i_mode = S_IFREG | 0644;
	KUNIT_ASSERT_EQ(test, insert_inode_locked(dup_existing), 0);
	ialloc_test_track_inode(test, ctx, dup_existing);

	ext4_free_inodes_set(sb, &ctx->gdps[0], 0);
	ext4_free_inodes_set(sb, &ctx->gdps[1], 0);
	ext4_free_inodes_set(sb, &ctx->gdps[3], 0);
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     140, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -EIO);
	KUNIT_EXPECT_TRUE(test,
			  (bool)EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[2]));

	/* 8. Descriptor claims free inodes in bg 3, but bitmap is all ones -> -ENOSPC */
	iblk = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(3));
	KUNIT_ASSERT_NOT_NULL(test, iblk);
	memset(iblk->cache_buf, 0xff, IALLOC_TEST_BLK_SIZE);
	ialloc_test_sync_ibitmap_csum(ctx, 3, true);
	ext4_free_inodes_set(sb, &ctx->gdps[2], 0);
	ext4_free_inodes_set(sb, &ctx->gdps[3], 5);
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     193, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -ENOSPC);

	/* 9. All groups report 0 free inodes -> -ENOSPC before loop */
	for (g = 0; g < IALLOC_TEST_NGROUPS; g++)
		ext4_free_inodes_set(sb, &ctx->gdps[g], 0);
	res = ext4_new_inode(nojournal, ctx->root_inode, S_IFREG | 0644, NULL,
			     0, NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(res), -ENOSPC);

	ext4_journal_stop(nojournal);
}

/* =========================================================================
 * 10. Orphans, Counters, & Lazy Init: ext4_orphan_get, ext4_count_*,
 *     ext4_init_inode_table
 * =========================================================================
 */

static void ialloc_test_orphan_get(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ialloc_test_blk *iblk1;
	struct inode *orphan, *got;

	/* Bounds checks */
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_orphan_get(sb, 5)), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test,
			PTR_ERR(ext4_orphan_get(sb, IALLOC_TEST_TOTAL_INODES + 1)),
			-EFSCORRUPTED);

	/* Clear bit in bitmap -> -EFSCORRUPTED */
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_orphan_get(sb, 65)), -EFSCORRUPTED);

	/* Mark bit 0 (ino 65) allocated in group 1 */
	iblk1 = ialloc_test_find_blk(ctx, ialloc_test_ibitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, iblk1);
	ext4_set_bit(0, iblk1->cache_buf);
	ialloc_test_sync_ibitmap_csum(ctx, 1, true);

	/* Bitmap read error propagates */
	clear_bitmap_uptodate(&iblk1->bh);
	clear_buffer_uptodate(&iblk1->bh);
	ctx->fail_read_eio = true;
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_orphan_get(sb, 65)), -EIO);
	ctx->fail_read_eio = false;
	set_buffer_uptodate(&iblk1->bh);
	set_bitmap_uptodate(&iblk1->bh);
	set_buffer_verified(&iblk1->bh);

	/* ext4_iget error propagates */
	ctx->fail_iget_err = -EIO;
	KUNIT_EXPECT_EQ(test, PTR_ERR(ext4_orphan_get(sb, 65)), -EIO);
	ctx->fail_iget_err = 0;

	/* Prepare mock orphan inode */
	orphan = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, orphan);
	orphan->i_ino = 65;
	ialloc_test_track_inode(test, ctx, orphan);
	ctx->iget_inode = orphan;

	/* 1. Valid unlinked orphan (i_nlink == 0, NEXT_ORPHAN <= max_ino) */
	orphan->i_mode = S_IFREG | 0644;
	clear_nlink(orphan);
	orphan->i_blocks = 8;
	NEXT_ORPHAN(orphan) = 0;
	got = ext4_orphan_get(sb, 65);
	KUNIT_ASSERT_PTR_EQ(test, got, orphan);
	iput(got);

	/* 2. Valid live truncating regular file orphan (i_nlink == 1) */
	set_nlink(orphan, 1);
	ext4_set_inode_flag(orphan, EXT4_INODE_EXTENTS);
	got = ext4_orphan_get(sb, 65);
	KUNIT_ASSERT_PTR_EQ(test, got, orphan);
	iput(got);

	/* 3. Live non-truncatable FIFO (i_nlink == 1) -> -EFSCORRUPTED */
	orphan->i_mode = S_IFIFO | 0600;
	got = ext4_orphan_get(sb, 65);
	KUNIT_EXPECT_EQ(test, PTR_ERR(got), -EFSCORRUPTED);

	/* 4. Unlinked orphan with NEXT_ORPHAN > max_ino zeroes i_blocks */
	orphan->i_mode = S_IFREG | 0644;
	clear_nlink(orphan);
	orphan->i_blocks = 24;
	NEXT_ORPHAN(orphan) = IALLOC_TEST_TOTAL_INODES + 50;
	got = ext4_orphan_get(sb, 65);
	KUNIT_EXPECT_EQ(test, PTR_ERR(got), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, orphan->i_blocks, (blkcnt_t)0);

	/* 5. Unlinked bad_inode zeroes i_blocks and returns -EFSCORRUPTED */
	NEXT_ORPHAN(orphan) = 0;
	orphan->i_blocks = 16;
	make_bad_inode(orphan);
	got = ext4_orphan_get(sb, 65);
	KUNIT_EXPECT_EQ(test, PTR_ERR(got), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, orphan->i_blocks, (blkcnt_t)0);
}

static void ialloc_test_count_free_inodes_and_dirs(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	/* Include 64-bit high-16-bit values (0x10000 + low) and a NULL gdp */
	ext4_free_inodes_set(sb, &ctx->gdps[0], 10);
	ext4_free_inodes_set(sb, &ctx->gdps[1], 0x10020);
	ext4_free_inodes_set(sb, &ctx->gdps[2], 999); /* masked out below */
	ext4_free_inodes_set(sb, &ctx->gdps[3], 5);

	ext4_used_dirs_set(sb, &ctx->gdps[0], 2);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 0x20003);
	ext4_used_dirs_set(sb, &ctx->gdps[2], 777); /* masked out below */
	ext4_used_dirs_set(sb, &ctx->gdps[3], 4);

	ctx->fail_gdp_mask = BIT(2);

	KUNIT_EXPECT_EQ(test, ext4_count_free_inodes(sb), 0x1002fUL);
	KUNIT_EXPECT_EQ(test, ext4_count_dirs(sb), 0x20009UL);
}

static void ialloc_test_init_inode_table(struct kunit *test)
{
	struct ialloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	int err;

	/* 1. Already EXT4_BG_INODE_ZEROED is a no-op */
	ctx->gdps[1].bg_flags = cpu_to_le16(EXT4_BG_INODE_ZEROED);
	err = ext4_init_inode_table(sb, 1, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->zeroout_calls, 0U);

	/* 2. Uninitialized group 1 zeroes all 16 blocks with barrier = 1 */
	ctx->gdps[1].bg_flags = cpu_to_le16(EXT4_BG_INODE_UNINIT);
	err = ext4_init_inode_table(sb, 1, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->zeroout_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_blk, ialloc_test_itable_blk(1));
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_num, 16);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_barrier, 1);
	KUNIT_EXPECT_TRUE(test,
			  !!(le16_to_cpu(ctx->gdps[1].bg_flags) &
			     EXT4_BG_INODE_ZEROED));
	KUNIT_EXPECT_TRUE(test,
			  (bool)ext4_group_desc_csum_verify(sb, 1,
							    &ctx->gdps[1]));

	/*
	 * 3. Initialized group 2 with itable_unused = 54 -> used_inos = 10 ->
	 *    used_blks = DIV_ROUND_UP(10, 4) = 3 -> zeroes remaining 13 blocks.
	 */
	ctx->gdps[2].bg_flags = 0;
	ext4_itable_unused_set(sb, &ctx->gdps[2], 54);
	err = ext4_init_inode_table(sb, 2, 0);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->zeroout_calls, 2U);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_blk,
			ialloc_test_itable_blk(2) + 3);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_num, 13);
	KUNIT_EXPECT_EQ(test, ctx->last_zeroout_barrier, 0);
	KUNIT_EXPECT_TRUE(test,
			  !!(le16_to_cpu(ctx->gdps[2].bg_flags) &
			     EXT4_BG_INODE_ZEROED));

	/* 4. Full inode table (itable_unused = 0 -> num = 0) skips zeroout, sets ZEROED */
	ctx->gdps[3].bg_flags = 0;
	ext4_itable_unused_set(sb, &ctx->gdps[3], 0);
	err = ext4_init_inode_table(sb, 3, 1);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->zeroout_calls, 2U);
	KUNIT_EXPECT_TRUE(test,
			  !!(le16_to_cpu(ctx->gdps[3].bg_flags) &
			     EXT4_BG_INODE_ZEROED));

	/* 5. Corrupt itable_unused > inodes_per_group returns 1 without ZEROED */
	ctx->gdps[3].bg_flags = 0;
	ext4_itable_unused_set(sb, &ctx->gdps[3], 100);
	err = ext4_init_inode_table(sb, 3, 0);
	KUNIT_EXPECT_EQ(test, err, 1);
	KUNIT_EXPECT_FALSE(test,
			   !!(le16_to_cpu(ctx->gdps[3].bg_flags) &
			      EXT4_BG_INODE_ZEROED));

	/* 6. Group 0 with used_inos (4) < EXT4_FIRST_INO (11) returns 1 */
	ctx->gdps[0].bg_flags = 0;
	ext4_itable_unused_set(sb, &ctx->gdps[0], 60);
	err = ext4_init_inode_table(sb, 0, 0);
	KUNIT_EXPECT_EQ(test, err, 1);
	KUNIT_EXPECT_FALSE(test,
			   !!(le16_to_cpu(ctx->gdps[0].bg_flags) &
			      EXT4_BG_INODE_ZEROED));

	/* 7. Zeroout I/O failure (-EIO) and emergency RO (-EROFS) */
	ctx->gdps[3].bg_flags = cpu_to_le16(EXT4_BG_INODE_UNINIT);
	ctx->fail_itable_zeroout_err = -EIO;
	err = ext4_init_inode_table(sb, 3, 0);
	ctx->fail_itable_zeroout_err = 0;
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_FALSE(test,
			   !!(le16_to_cpu(ctx->gdps[3].bg_flags) &
			      EXT4_BG_INODE_ZEROED));

	set_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	err = ext4_init_inode_table(sb, 3, 0);
	clear_bit(EXT4_FLAGS_EMERGENCY_RO, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, err, -EROFS);
}

static struct kunit_case ialloc_test_cases[] = {
	KUNIT_CASE_PARAM(ialloc_test_mark_bitmap_end,
			 mark_bitmap_end_gen_params),
	KUNIT_CASE(ialloc_test_end_bitmap_read_ok),
	KUNIT_CASE(ialloc_test_end_bitmap_read_ioerr),
	KUNIT_CASE(ialloc_test_validate_ibitmap_csum),
	KUNIT_CASE(ialloc_test_validate_ibitmap_short_circuits),
	KUNIT_CASE(ialloc_test_read_ibitmap_disk_and_cache),
	KUNIT_CASE(ialloc_test_read_ibitmap_uninit),
	KUNIT_CASE(ialloc_test_read_ibitmap_errors),
	KUNIT_CASE(ialloc_test_free_inode_reg_and_dir),
	KUNIT_CASE(ialloc_test_free_inode_guards),
	KUNIT_CASE(ialloc_test_free_inode_corrupt_and_journal_errors),
	KUNIT_CASE(ialloc_test_orlov_topdir_spread),
	KUNIT_CASE(ialloc_test_orlov_subdir_and_thresholds),
	KUNIT_CASE(ialloc_test_orlov_flex_bg_and_fallback),
	KUNIT_CASE(ialloc_test_find_group_other_non_flex),
	KUNIT_CASE(ialloc_test_find_group_other_flex_bg),
	KUNIT_CASE(ialloc_test_recently_deleted),
	KUNIT_CASE(ialloc_test_find_inode_bit),
	KUNIT_CASE(ialloc_test_mark_inode_used_uninit),
	KUNIT_CASE(ialloc_test_mark_inode_used_errors),
	KUNIT_CASE(ialloc_test_xattr_credits_for_new_inode),
	KUNIT_CASE(ialloc_test_new_inode_types_and_flags),
	KUNIT_CASE(ialloc_test_new_inode_ownership_and_projid),
	KUNIT_CASE(ialloc_test_new_inode_goal_uninit_and_corrupt_skip),
	KUNIT_CASE(ialloc_test_new_inode_errors),
	KUNIT_CASE(ialloc_test_orphan_get),
	KUNIT_CASE(ialloc_test_count_free_inodes_and_dirs),
	KUNIT_CASE(ialloc_test_init_inode_table),
	{}
};

static struct kunit_suite ialloc_test_suite = {
	.name = "ext4_ialloc_test",
	.init = ialloc_test_init,
	.test_cases = ialloc_test_cases,
};

kunit_test_suites(&ialloc_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 inode allocation and deallocation");
MODULE_LICENSE("GPL");
