// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/resize.c
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fscrypt.h>
#include <linux/iversion.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "mballoc.h"

#define RESIZE_TEST_BLK_SIZE		1024U
#define RESIZE_TEST_BLK_BITS		10U
#define RESIZE_TEST_BLOCKS_PER_GRP	1024U
#define RESIZE_TEST_FIRST_DATA_BLK	1U
#define RESIZE_TEST_INODE_SIZE		256U
#define RESIZE_TEST_EXTRA_ISIZE		32U
#define RESIZE_TEST_INODES_PER_GRP	32U
#define RESIZE_TEST_ITB_PER_GRP		8U
#define RESIZE_TEST_DESC_SIZE		64U
#define RESIZE_TEST_DESC_PER_BLK	16U
#define RESIZE_TEST_DESC_PER_BLK_BITS	4U

struct resize_mock_blk {
	struct list_head node;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	u8 *data;
};

struct resize_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block *es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct folio *bh_folio;
	struct list_head blks;
	struct buffer_head iloc_bh;
	u8 iloc_buf[RESIZE_TEST_BLK_SIZE];
	struct inode *resize_inode;
	ext4_fsblk_t device_blocks;
	ext4_fsblk_t last_freed_blk;
	unsigned long last_freed_count;
	int zeroout_calls;
	sector_t zeroout_blks;
	int zeroout_err;
	bool sb_getblk_fail;
	bool mb_inited;
};

static struct kunit *active_resize_test;

static struct inode *resize_test_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	inode_set_iversion(&ei->vfs_inode, 1);
	ei->i_prealloc_node = RB_ROOT;
	rwlock_init(&ei->i_prealloc_lock);
	ext4_es_init_tree(&ei->i_es_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_list);
	spin_lock_init(&ei->i_raw_lock);
	spin_lock_init(&ei->i_block_reservation_lock);
	ext4_init_pending_tree(&ei->i_pending_tree);
	init_rwsem(&ei->xattr_sem);
	INIT_LIST_HEAD(&ei->i_rsv_conversion_list);
	spin_lock_init(&ei->i_completed_io_lock);
	INIT_LIST_HEAD(&ei->i_orphan);
	ext4_fc_init_inode(&ei->vfs_inode);
	spin_lock_init(&ei->i_fc_lock);
	ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	ei->i_extra_isize = RESIZE_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void resize_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void resize_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **resize_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations resize_test_sops = {
	.alloc_inode	= resize_test_alloc_inode,
	.free_inode	= resize_test_free_inode,
	.evict_inode	= resize_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= resize_test_get_dquots,
#endif
};

static int resize_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static void resize_test_sb_upd_work(struct work_struct *work)
{
}

static void resize_test_err_timer(struct timer_list *t)
{
}

static struct file_system_type resize_test_fs_type = {
	.name		 = "ext4_resize_kunit",
	.init_fs_context = resize_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void resize_init_mock_bh(struct buffer_head *bh, struct folio *folio,
				u8 *data, size_t size, sector_t blocknr)
{
	memset(bh, 0, sizeof(*bh));
	bh->b_folio = folio;
	bh->b_data = (char *)data;
	bh->b_size = size;
	bh->b_blocknr = blocknr;
	atomic_set(&bh->b_count, 2);
	set_buffer_uptodate(bh);
}

static struct buffer_head *resize_lookup_bh(struct resize_test_ctx *ctx,
					    ext4_fsblk_t blocknr)
{
	struct resize_mock_blk *blk;

	list_for_each_entry(blk, &ctx->blks, node) {
		if (blk->blocknr == blocknr)
			return &blk->bh;
	}
	return NULL;
}

static struct buffer_head *resize_get_or_create_bh(struct resize_test_ctx *ctx,
						   ext4_fsblk_t blocknr)
{
	struct buffer_head *bh;
	struct resize_mock_blk *blk;

	bh = resize_lookup_bh(ctx, blocknr);
	if (bh) {
		get_bh(bh);
		return bh;
	}

	blk = kzalloc_obj(*blk);
	if (!blk)
		return NULL;
	blk->data = kzalloc(RESIZE_TEST_BLK_SIZE, GFP_KERNEL);
	if (!blk->data) {
		kfree(blk);
		return NULL;
	}
	blk->blocknr = blocknr;
	resize_init_mock_bh(&blk->bh, ctx->bh_folio, blk->data,
			    RESIZE_TEST_BLK_SIZE, blocknr);
	list_add_tail(&blk->node, &ctx->blks);
	get_bh(&blk->bh);
	return &blk->bh;
}

static struct buffer_head *resize_sb_getblk_stub(struct super_block *sb,
						 ext4_fsblk_t block)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;

	if (!test)
		return NULL;
	ctx = test->priv;
	if (!ctx || sb != ctx->sb || ctx->sb_getblk_fail)
		return NULL;

	return resize_get_or_create_bh(ctx, block);
}

static struct buffer_head *resize_sb_bread_stub(struct super_block *sb,
						sector_t block,
						blk_opf_t op_flags)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;
	struct buffer_head *bh;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return ERR_PTR(-EIO);
	if ((ext4_fsblk_t)block >= ctx->device_blocks)
		return ERR_PTR(-EIO);

	bh = resize_get_or_create_bh(ctx, block);
	if (!bh)
		return ERR_PTR(-ENOMEM);
	return bh;
}

static int resize_zeroout_stub(struct super_block *sb, sector_t blk,
			       sector_t nr_blks, gfp_t gfp_mask)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;
	sector_t i;

	if (!test)
		return 0;
	ctx = test->priv;
	if (!ctx)
		return 0;
	if (ctx->zeroout_err)
		return ctx->zeroout_err;

	ctx->zeroout_calls++;
	ctx->zeroout_blks += nr_blks;
	for (i = 0; i < nr_blks; i++) {
		struct buffer_head *bh = resize_lookup_bh(ctx, blk + i);

		if (bh)
			memset(bh->b_data, 0, bh->b_size);
	}
	return 0;
}

static struct inode *resize_iget_stub(struct super_block *sb, unsigned long ino,
				      ext4_iget_flags flags,
				      const char *function, unsigned int line)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;

	if (!test)
		return ERR_PTR(-ENOENT);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return ERR_PTR(-ENOENT);
	if (ino == EXT4_RESIZE_INO && ctx->resize_inode) {
		ihold(ctx->resize_inode);
		return ctx->resize_inode;
	}
	return ERR_PTR(-ENOENT);
}

static int resize_get_inode_loc_stub(struct inode *inode,
				     struct ext4_iloc *iloc)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;

	if (!test)
		return -EIO;
	ctx = test->priv;
	if (!ctx)
		return -EIO;

	get_bh(&ctx->iloc_bh);
	iloc->bh = &ctx->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int resize_mark_iloc_dirty_stub(handle_t *handle, struct inode *inode,
				       struct ext4_iloc *iloc)
{
	brelse(iloc->bh);
	iloc->bh = NULL;
	return 0;
}

static int resize_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
					const char *func, unsigned int line)
{
	return 0;
}

static void resize_free_blocks_stub(handle_t *handle, struct inode *inode,
				    struct buffer_head *bh, ext4_fsblk_t block,
				    unsigned long count, int flags)
{
	struct kunit *test = active_resize_test;
	struct resize_test_ctx *ctx;

	if (!test)
		return;
	ctx = test->priv;
	if (!ctx)
		return;

	ctx->last_freed_blk = block;
	ctx->last_freed_count = count;
	if (bh)
		brelse(bh);
}

static void resize_test_cleanup(struct resize_test_ctx *ctx)
{
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct flex_groups **flex_groups;
	struct resize_mock_blk *blk, *tmp;
	unsigned int i;

	active_resize_test = NULL;

	cancel_work_sync(&sbi->s_sb_upd_work);
	timer_delete_sync(&sbi->s_err_report);

	if (ctx->resize_inode) {
		iput(ctx->resize_inode);
		ctx->resize_inode = NULL;
	}

	if (ctx->mb_inited && sb) {
		ext4_mb_release(sb);
		ctx->mb_inited = false;
	}

	if (sb) {
		deactivate_super(sb);
		ctx->sb = NULL;
	}
	rcu_barrier();

	percpu_counter_destroy(&sbi->s_freeclusters_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);

	flex_groups = rcu_access_pointer(sbi->s_flex_groups);
	if (flex_groups) {
		for (i = 0; i < sbi->s_flex_groups_allocated; i++)
			kvfree(flex_groups[i]);
		kvfree(flex_groups);
		rcu_assign_pointer(sbi->s_flex_groups, NULL);
		sbi->s_flex_groups_allocated = 0;
	}

	kvfree(rcu_access_pointer(sbi->s_group_desc));
	rcu_assign_pointer(sbi->s_group_desc, NULL);

	list_for_each_entry_safe(blk, tmp, &ctx->blks, node) {
		list_del(&blk->node);
		kfree(blk->data);
		kfree(blk);
	}

	if (ctx->bh_folio) {
		folio_put(ctx->bh_folio);
		ctx->bh_folio = NULL;
	}

	if (ctx->bdev.bd_stats) {
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
	}
}

KUNIT_DEFINE_ACTION_WRAPPER(resize_test_cleanup_action, resize_test_cleanup,
			    struct resize_test_ctx *);

static int resize_test_set_super(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

/*
 * Configure an in-memory ext4 filesystem with @ngroups initial groups,
 * optional @reserved_gdb reserved GDT blocks (enabling RESIZE_INODE and
 * populating the resize inode's DIND + reserved GDT blocks), optional
 * flex_bg (@log_flex > 0), and @last_grp_blks blocks in the final group.
 */
static void resize_configure_fs(struct kunit *test, ext4_group_t ngroups,
				u16 reserved_gdb, u8 log_flex,
				u32 last_grp_blks)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = ctx->es;
	struct buffer_head **group_desc;
	unsigned long gdb_count;
	ext4_fsblk_t total_blks = RESIZE_TEST_FIRST_DATA_BLK;
	ext4_fsblk_t total_free_blks = 0;
	u32 total_inodes = ngroups * RESIZE_TEST_INODES_PER_GRP;
	ext4_group_t g;
	int ret;

	KUNIT_ASSERT_GT(test, ngroups, 0U);
	KUNIT_ASSERT_LE(test, last_grp_blks, RESIZE_TEST_BLOCKS_PER_GRP);

	es->s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	es->s_log_block_size = cpu_to_le32(0);
	es->s_log_cluster_size = cpu_to_le32(0);
	es->s_first_data_block = cpu_to_le32(RESIZE_TEST_FIRST_DATA_BLK);
	es->s_blocks_per_group = cpu_to_le32(RESIZE_TEST_BLOCKS_PER_GRP);
	es->s_clusters_per_group = cpu_to_le32(RESIZE_TEST_BLOCKS_PER_GRP);
	es->s_inodes_per_group = cpu_to_le32(RESIZE_TEST_INODES_PER_GRP);
	es->s_inodes_count = cpu_to_le32(total_inodes);
	es->s_free_inodes_count = cpu_to_le32(total_inodes - 10);
	es->s_first_ino = cpu_to_le32(11);
	es->s_inode_size = cpu_to_le16(RESIZE_TEST_INODE_SIZE);
	es->s_desc_size = cpu_to_le16(RESIZE_TEST_DESC_SIZE);
	es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	es->s_state = cpu_to_le16(EXT4_VALID_FS);
	es->s_errors = cpu_to_le16(EXT4_ERRORS_CONTINUE);
	es->s_checksum_type = EXT4_CRC32C_CHKSUM;
	es->s_reserved_gdt_blocks = cpu_to_le16(reserved_gdb);
	generate_random_uuid(es->s_uuid);

	ext4_set_feature_extents(sb);
	ext4_set_feature_64bit(sb);
	ext4_set_feature_filetype(sb);
	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_metadata_csum(sb);
	if (reserved_gdb > 0)
		ext4_set_feature_resize_inode(sb);
	if (log_flex > 0) {
		ext4_set_feature_flex_bg(sb);
		es->s_log_groups_per_flex = log_flex;
		sbi->s_log_groups_per_flex = log_flex;
	}

	sbi->s_csum_seed = ext4_chksum(~0, es->s_uuid, sizeof(es->s_uuid));
	sbi->s_groups_count = ngroups;
	gdb_count = DIV_ROUND_UP(ngroups, RESIZE_TEST_DESC_PER_BLK);
	sbi->s_gdb_count = gdb_count;

	group_desc = kvcalloc(gdb_count, sizeof(*group_desc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, group_desc);
	rcu_assign_pointer(sbi->s_group_desc, group_desc);
	for (g = 0; g < gdb_count; g++) {
		struct buffer_head *bh;

		bh = resize_get_or_create_bh(ctx,
					     RESIZE_TEST_FIRST_DATA_BLK + 1 + g);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		brelse(bh);
		group_desc[g] = bh;
	}

	for (g = 0; g < ngroups; g++) {
		u32 grp_blks = (g == ngroups - 1) ?
			       last_grp_blks : RESIZE_TEST_BLOCKS_PER_GRP;
		ext4_fsblk_t start = ext4_group_first_block_no(sb, g);
		unsigned int overhead = ext4_bg_num_gdb(sb, g);
		unsigned int used_meta;
		ext4_fsblk_t bb_blk, ib_blk, it_blk;
		struct buffer_head *bb_bh, *ib_bh;
		struct ext4_group_desc *gdp;
		u32 free_blks;

		if (ext4_bg_has_super(sb, g))
			overhead += 1 + reserved_gdb;

		bb_blk = start + overhead;
		ib_blk = bb_blk + 1;
		it_blk = ib_blk + 1;
		used_meta = overhead + 2 + RESIZE_TEST_ITB_PER_GRP;
		if (g == 0 && reserved_gdb > 0)
			used_meta++;

		bb_bh = resize_get_or_create_bh(ctx, bb_blk);
		ib_bh = resize_get_or_create_bh(ctx, ib_blk);
		KUNIT_ASSERT_NOT_NULL(test, bb_bh);
		KUNIT_ASSERT_NOT_NULL(test, ib_bh);

		mb_set_bits(bb_bh->b_data, 0, used_meta);
		ext4_mark_bitmap_end(grp_blks, sb->s_blocksize * 8,
				     bb_bh->b_data);
		ext4_mark_bitmap_end(RESIZE_TEST_INODES_PER_GRP,
				     sb->s_blocksize * 8, ib_bh->b_data);

		gdp = ext4_get_group_desc(sb, g, NULL);
		KUNIT_ASSERT_NOT_NULL(test, gdp);
		memset(gdp, 0, RESIZE_TEST_DESC_SIZE);
		ext4_block_bitmap_set(sb, gdp, bb_blk);
		ext4_inode_bitmap_set(sb, gdp, ib_blk);
		ext4_inode_table_set(sb, gdp, it_blk);
		free_blks = grp_blks - used_meta;
		ext4_free_group_clusters_set(sb, gdp, free_blks);
		ext4_free_inodes_set(sb, gdp, RESIZE_TEST_INODES_PER_GRP);
		ext4_itable_unused_set(sb, gdp, RESIZE_TEST_INODES_PER_GRP);
		gdp->bg_flags = cpu_to_le16(EXT4_BG_INODE_ZEROED);
		ext4_block_bitmap_csum_set(sb, gdp, bb_bh);
		ext4_inode_bitmap_csum_set(sb, gdp, ib_bh);
		ext4_group_desc_csum_set(sb, g, gdp);

		brelse(bb_bh);
		brelse(ib_bh);

		total_blks += grp_blks;
		total_free_blks += free_blks;
	}

	ext4_blocks_count_set(es, total_blks);
	ext4_free_blocks_count_set(es, total_free_blks);
	ext4_r_blocks_count_set(es, div_u64(total_blks, 20));
	sbi->s_blockfile_groups = ngroups;

	if (reserved_gdb > 0) {
		ext4_fsblk_t dind_blk;
		struct buffer_head *dind_bh;
		__le32 *dind_data;
		unsigned int res;
		unsigned int total_rsv_blks = 1;

		dind_blk = ext4_group_first_block_no(sb, 0) + 1 + gdb_count +
			   reserved_gdb + 2 + RESIZE_TEST_ITB_PER_GRP;
		dind_bh = resize_get_or_create_bh(ctx, dind_blk);
		KUNIT_ASSERT_NOT_NULL(test, dind_bh);
		dind_data = (__le32 *)dind_bh->b_data;

		for (res = 0; res < reserved_gdb; res++) {
			ext4_fsblk_t pri_blk = RESIZE_TEST_FIRST_DATA_BLK + 1 +
					       gdb_count + res;
			struct buffer_head *pri_bh;
			__le32 *pri_data;
			unsigned int three = 1, five = 5, seven = 7;
			unsigned int bgrp, idx = 0;

			dind_data[(gdb_count + res) %
				  EXT4_ADDR_PER_BLOCK(sb)] =
				cpu_to_le32(pri_blk);
			pri_bh = resize_get_or_create_bh(ctx, pri_blk);
			KUNIT_ASSERT_NOT_NULL(test, pri_bh);
			pri_data = (__le32 *)pri_bh->b_data;

			while ((bgrp = ext4_list_backups(sb, &three, &five,
							 &seven)) < ngroups) {
				pri_data[idx++] = cpu_to_le32(
					bgrp * RESIZE_TEST_BLOCKS_PER_GRP +
					pri_blk);
			}
			total_rsv_blks += 1 + idx;
			brelse(pri_bh);
		}
		brelse(dind_bh);

		ctx->resize_inode = new_inode(sb);
		KUNIT_ASSERT_NOT_NULL(test, ctx->resize_inode);
		ctx->resize_inode->i_ino = EXT4_RESIZE_INO;
		ctx->resize_inode->i_mode = S_IFREG | 0600;
		ctx->resize_inode->i_blkbits = RESIZE_TEST_BLK_BITS;
		ctx->resize_inode->i_blocks = total_rsv_blks *
					      (RESIZE_TEST_BLK_SIZE >> 9);
		EXT4_I(ctx->resize_inode)->i_data[EXT4_DIND_BLOCK] =
			cpu_to_le32(dind_blk);
	}

	if (log_flex > 0) {
		ret = ext4_fill_flex_info_test(sb);
		KUNIT_ASSERT_EQ(test, ret, 1);
	}

	ret = ext4_mb_init(sb);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->mb_inited = true;

	ret = percpu_counter_init(&sbi->s_freeclusters_counter,
				  total_free_blks, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = percpu_counter_init(&sbi->s_freeinodes_counter,
				  total_inodes - 10, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ext4_calculate_overhead(sb);
	es->s_overhead_clusters = cpu_to_le32(sbi->s_overhead);
	ext4_superblock_csum_set(sb);
}

static int resize_test_init(struct kunit *test)
{
	struct resize_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct buffer_head *sbh;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	INIT_LIST_HEAD(&ctx->blks);
	ctx->device_blocks = 1 + 64 * RESIZE_TEST_BLOCKS_PER_GRP;
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	KUNIT_ASSERT_TRUE(test, ctx->bdev.bd_stats != NULL);

	ctx->bh_folio = folio_alloc(GFP_KERNEL, 0);
	if (!ctx->bh_folio) {
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "folio_alloc failed");
	}

	fc = fs_context_for_mount(&resize_test_fs_type, 0);
	if (IS_ERR(fc)) {
		folio_put(ctx->bh_folio);
		ctx->bh_folio = NULL;
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "fs_context_for_mount failed: %ld",
				     PTR_ERR(fc));
	}
	sb = sget_fc(fc, NULL, resize_test_set_super);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		folio_put(ctx->bh_folio);
		ctx->bh_folio = NULL;
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "sget_fc failed: %ld", PTR_ERR(sb));
	}
	ctx->sb = sb;
	sbi = &ctx->sbi;
	INIT_WORK(&sbi->s_sb_upd_work, resize_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, resize_test_err_timer, 0);
	up_write(&sb->s_umount);

	ret = kunit_add_action_or_reset(test, resize_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sb->s_fs_info = sbi;
	sb->s_op = &resize_test_sops;
	sb->s_blocksize = RESIZE_TEST_BLK_SIZE;
	sb->s_blocksize_bits = RESIZE_TEST_BLK_BITS;
	sb->s_magic = EXT4_SUPER_MAGIC;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	INIT_LIST_HEAD(&sb->s_inodes);
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif

	strscpy(sb->s_id, "ext4_resize_kunit", sizeof(sb->s_id));
	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = RESIZE_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_disk.part0 = &ctx->bdev;
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	sbi->s_sb = sb;
	sbi->s_blockgroup_lock = &ctx->bgl;
	bgl_lock_init(&ctx->bgl);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_es_lock);
	INIT_LIST_HEAD(&sbi->s_es_list);
	mutex_init(&sbi->s_fc_lock);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_STAGING]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_STAGING]);
	ratelimit_state_init(&sbi->s_err_ratelimit_state, 5 * HZ, 10);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state, 5 * HZ, 10);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state, 5 * HZ, 10);

	sbi->s_blocks_per_group = RESIZE_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = RESIZE_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_inodes_per_group = RESIZE_TEST_INODES_PER_GRP;
	sbi->s_inode_size = RESIZE_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = RESIZE_TEST_BLK_SIZE / RESIZE_TEST_INODE_SIZE;
	sbi->s_itb_per_group = RESIZE_TEST_ITB_PER_GRP;
	sbi->s_desc_size = RESIZE_TEST_DESC_SIZE;
	sbi->s_desc_per_block = RESIZE_TEST_DESC_PER_BLK;
	sbi->s_desc_per_block_bits = RESIZE_TEST_DESC_PER_BLK_BITS;
	sbi->s_first_ino = 11;
	sbi->s_mount_state = EXT4_VALID_FS;
	set_opt(sb, ERRORS_CONT);

	sbh = resize_get_or_create_bh(ctx, RESIZE_TEST_FIRST_DATA_BLK);
	KUNIT_ASSERT_NOT_NULL(test, sbh);
	brelse(sbh);
	sbi->s_sbh = sbh;
	ctx->es = (struct ext4_super_block *)sbh->b_data;
	sbi->s_es = ctx->es;

	resize_init_mock_bh(&ctx->iloc_bh, ctx->bh_folio, ctx->iloc_buf,
			    RESIZE_TEST_BLK_SIZE, 99);

	active_resize_test = test;
	kunit_activate_static_stub(test, ext4_sb_getblk, resize_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_sb_bread, resize_sb_bread_stub);
	kunit_activate_static_stub(test, ext4_resize_issue_zeroout,
				   resize_zeroout_stub);
	kunit_activate_static_stub(test, __ext4_iget, resize_iget_stub);
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   resize_get_inode_loc_stub);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   resize_mark_iloc_dirty_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   resize_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   resize_free_blocks_stub);

	return 0;
}

/*
 * Test 1: ext4_resize_begin() precondition checks and EXT4_FLAGS_RESIZING
 * mutual exclusion with ext4_resize_end().
 */
static void test_resize_begin_and_end_guards(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	resize_configure_fs(test, 2, 0, 0, RESIZE_TEST_BLOCKS_PER_GRP);

	/* Non-zero s_reserved_gdt_blocks without resize_inode -> -EFSCORRUPTED */
	ctx->es->s_reserved_gdt_blocks = cpu_to_le16(2);
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), -EFSCORRUPTED);
	ctx->es->s_reserved_gdt_blocks = 0;
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;

	/* Mounted from backup superblock -> -EPERM */
	ctx->sbi.s_sbh->b_blocknr = 1025;
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), -EPERM);
	ctx->sbi.s_sbh->b_blocknr = RESIZE_TEST_FIRST_DATA_BLK;

	/* Filesystem has recorded errors -> -EPERM */
	ctx->sbi.s_mount_state |= EXT4_ERROR_FS;
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), -EPERM);
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;

	/* sparse_super2 online resize unsupported -> -EOPNOTSUPP */
	ext4_set_feature_sparse_super2(sb);
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), -EOPNOTSUPP);
	ext4_clear_feature_sparse_super2(sb);

	/* Valid begin acquires EXT4_FLAGS_RESIZING; second call gets -EBUSY */
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), 0);
	KUNIT_EXPECT_TRUE(test, test_bit(EXT4_FLAGS_RESIZING,
					 &ctx->sbi.s_ext4_flags));
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), -EBUSY);

	/* ext4_resize_end releases lock so subsequent begin succeeds */
	KUNIT_EXPECT_EQ(test, ext4_resize_end(sb, false), 0);
	KUNIT_EXPECT_FALSE(test, test_bit(EXT4_FLAGS_RESIZING,
					  &ctx->sbi.s_ext4_flags));
	KUNIT_EXPECT_EQ(test, ext4_resize_begin(sb), 0);
	KUNIT_EXPECT_EQ(test, ext4_resize_end(sb, false), 0);
}

/*
 * Test 2: ext4_list_backups() sequence generation (sparse_super, dense, and
 * sparse_super2) and verify_reserved_gdb() validation.
 */
static void test_list_backups_and_verify_reserved_gdb(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	static const unsigned int expected_sparse[] = {
		1, 3, 5, 7, 9, 25, 27, 49, 81
	};
	unsigned int three, five, seven;
	struct buffer_head *pri_bh;
	__le32 *pri_data;
	unsigned int i;

	resize_configure_fs(test, 4, 0, 0, RESIZE_TEST_BLOCKS_PER_GRP);

	/* 1. sparse_super yields sorted powers of 3, 5, and 7 */
	three = 1;
	five = 5;
	seven = 7;
	for (i = 0; i < ARRAY_SIZE(expected_sparse); i++) {
		KUNIT_EXPECT_EQ_MSG(test,
				    ext4_list_backups(sb, &three, &five, &seven),
				    expected_sparse[i], "sparse index %u", i);
	}

	/* 2. Without sparse_super: every consecutive group 1, 2, 3, 4 */
	ext4_clear_feature_sparse_super(sb);
	three = 1;
	five = 5;
	seven = 7;
	for (i = 1; i <= 4; i++)
		KUNIT_EXPECT_EQ(test,
				ext4_list_backups(sb, &three, &five, &seven),
				i);
	ext4_set_feature_sparse_super(sb);

	/* 3. sparse_super2: reads s_backup_bgs[0..1], skipping 0, then UINT_MAX */
	ext4_set_feature_sparse_super2(sb);
	ctx->es->s_backup_bgs[0] = 0;
	ctx->es->s_backup_bgs[1] = cpu_to_le32(12);
	three = 1;
	five = 5;
	seven = 7;
	KUNIT_EXPECT_EQ(test, ext4_list_backups(sb, &three, &five, &seven), 12U);
	KUNIT_EXPECT_EQ(test, ext4_list_backups(sb, &three, &five, &seven),
			UINT_MAX);
	ext4_clear_feature_sparse_super2(sb);

	/* 4. verify_reserved_gdb: valid backup list for groups < 10 (5 backups) */
	pri_bh = resize_get_or_create_bh(ctx, 3);
	KUNIT_ASSERT_NOT_NULL(test, pri_bh);
	pri_data = (__le32 *)pri_bh->b_data;
	for (i = 0; i < 5; i++)
		pri_data[i] = cpu_to_le32(expected_sparse[i] *
					  RESIZE_TEST_BLOCKS_PER_GRP + 3);
	KUNIT_EXPECT_EQ(test, ext4_verify_reserved_gdb_test(sb, 10, pri_bh), 5);

	/* Corrupted backup block entry -> -EINVAL */
	pri_data[2] = cpu_to_le32(99999);
	KUNIT_EXPECT_EQ(test, ext4_verify_reserved_gdb_test(sb, 10, pri_bh),
			-EINVAL);

	/* Exceeding EXT4_ADDR_PER_BLOCK (256 on 1K blocks) -> -EFBIG */
	ext4_clear_feature_sparse_super(sb);
	for (i = 0; i <= 257 && i < RESIZE_TEST_BLK_SIZE / sizeof(__le32); i++)
		pri_data[i] = cpu_to_le32((i + 1) * RESIZE_TEST_BLOCKS_PER_GRP +
					  3);
	/*
	 * Allocate an oversized buffer temporarily so 257 entries can match
	 * before hitting ++gdbackups > EXT4_ADDR_PER_BLOCK(sb) (256).
	 */
	{
		__le32 *big_buf = kunit_kcalloc(test, 260, sizeof(__le32),
						GFP_KERNEL);
		u8 *orig_data = (u8 *)pri_bh->b_data;

		KUNIT_ASSERT_NOT_NULL(test, big_buf);
		for (i = 0; i < 258; i++)
			big_buf[i] = cpu_to_le32((i + 1) *
						 RESIZE_TEST_BLOCKS_PER_GRP +
						 3);
		pri_bh->b_data = (char *)big_buf;
		KUNIT_EXPECT_EQ(test,
				ext4_verify_reserved_gdb_test(sb, 259, pri_bh),
				-EFBIG);
		pri_bh->b_data = (char *)orig_data;
	}
	brelse(pri_bh);
}

/*
 * Test 3: verify_group_input() boundary and overlap checks when adding sparse
 * backup group 3 (which carries superblock + GDT overhead at group start).
 */
static void test_verify_group_input_rejections_and_acceptance(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_new_group_data input;
	ext4_fsblk_t start;
	unsigned int overhead;

	/* Configure 3 full groups (0..2); next group to add is sparse group 3 */
	resize_configure_fs(test, 3, 1, 0, RESIZE_TEST_BLOCKS_PER_GRP);
	start = ext4_blocks_count(ctx->es);
	/* Group 3 has backup super (1) + 1 GDT + 1 reserved GDT = 3 blocks */
	overhead = 3;

	memset(&input, 0, sizeof(input));
	input.group = 3;
	input.blocks_count = RESIZE_TEST_BLOCKS_PER_GRP;
	input.reserved_blocks = 50;
	input.block_bitmap = start + overhead;
	input.inode_bitmap = start + overhead + 1;
	input.inode_table = start + overhead + 2;

	/* 1. Wrong group number */
	input.group = 4;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.group = 3;

	/* 2. Previous last group not full */
	ext4_blocks_count_set(ctx->es, start - 10);
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	ext4_blocks_count_set(ctx->es, start);

	/* 3. reserved_blocks > blocks_count / 5 */
	input.reserved_blocks = 205;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.reserved_blocks = 50;

	/* 4. blocks_count smaller than metadata (free_blocks_count < 0) */
	input.blocks_count = 10;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.blocks_count = RESIZE_TEST_BLOCKS_PER_GRP;

	/* 5. Last block beyond underlying device -> -EIO */
	ctx->device_blocks = start + 100;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input), -EIO);
	ctx->device_blocks = 1 + 64 * RESIZE_TEST_BLOCKS_PER_GRP;

	/* 6. Bitmaps or inode table outside [start, end) */
	input.block_bitmap = start - 1;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.block_bitmap = start + overhead;

	input.inode_bitmap = start + RESIZE_TEST_BLOCKS_PER_GRP;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_bitmap = start + overhead + 1;

	input.inode_table = start + RESIZE_TEST_BLOCKS_PER_GRP -
			    RESIZE_TEST_ITB_PER_GRP + 1;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_table = start + overhead + 2;

	/* 7. Bitmap collision and bitmap inside inode table */
	input.inode_bitmap = input.block_bitmap;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_bitmap = input.inode_table + 3;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_bitmap = start + overhead + 1;

	input.block_bitmap = input.inode_table;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.block_bitmap = start + overhead;

	/* 8. Bitmaps or inode table overlapping superblock/GDT overhead */
	input.block_bitmap = start + overhead - 1;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.block_bitmap = start + overhead;

	input.inode_bitmap = start;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_bitmap = start + overhead + 1;

	input.inode_table = start + 1;
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input),
			-EINVAL);
	input.inode_table = start + overhead + 2;

	/* 9. Valid group input accepted with exact free_clusters_count */
	KUNIT_EXPECT_EQ(test, ext4_verify_group_input_test(sb, &input), 0);
	/* 1024 - 2 (bitmaps) - 3 (overhead) - 8 (itable) = 1011 */
	KUNIT_EXPECT_EQ(test, input.free_clusters_count, 1011U);
}

/*
 * Test 4: alloc_flex_gd(), ext4_setup_next_flex_gd(), and
 * ext4_alloc_group_tables() packed flex_bg layout and -ENOSPC handling.
 */
static void test_alloc_setup_flex_gd_and_group_tables(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_new_flex_group_data *flex_gd;
	ext4_fsblk_t g4_start;

	/* 4 initial groups (1 flex_bg of size 4, log_flex = 2), meta_bg mode */
	resize_configure_fs(test, 4, 0, 2, RESIZE_TEST_BLOCKS_PER_GRP);
	ext4_set_feature_meta_bg(sb);
	ctx->es->s_first_meta_bg = 0;

	/* 1. alloc_flex_gd shrinks allocation when adding only 1 group (4..4) */
	flex_gd = ext4_alloc_flex_gd_test(16, 4, 4);
	KUNIT_ASSERT_NOT_NULL(test, flex_gd);
	KUNIT_EXPECT_EQ(test, flex_gd->resize_bg, 1U);
	ext4_free_flex_gd_test(flex_gd);

	/* 2. Full flex_bg batch (groups 4..7, with group 7 partial = 500 blks) */
	flex_gd = ext4_alloc_flex_gd_test(4, 3, 7);
	KUNIT_ASSERT_NOT_NULL(test, flex_gd);
	KUNIT_EXPECT_EQ(test, flex_gd->resize_bg, 4U);

	/* No-op when target equals current blocks_count */
	KUNIT_EXPECT_EQ(test,
			ext4_setup_next_flex_gd_test(sb, flex_gd,
						     ext4_blocks_count(ctx->es)),
			0);

	/* Setup groups 4, 5, 6 (1024 blks each) + group 7 (500 blks) */
	KUNIT_EXPECT_EQ(test,
			ext4_setup_next_flex_gd_test(sb, flex_gd,
						     1 + 7 * 1024 + 500),
			1);
	KUNIT_EXPECT_EQ(test, flex_gd->count, 4U);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[0].group, 4U);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[3].group, 7U);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[3].blocks_count, 500U);
	/* Final group clears BG_BLOCK_UNINIT under group_desc_csum */
	KUNIT_EXPECT_NE(test, flex_gd->bg_flags[0] & EXT4_BG_BLOCK_UNINIT, 0);
	KUNIT_EXPECT_EQ(test, flex_gd->bg_flags[3] & EXT4_BG_BLOCK_UNINIT, 0);

	/*
	 * 3. ext4_alloc_group_tables: group 4 has no super/meta_bg overhead
	 * (group 5 has sparse backup super -> overhead=1, stopping contiguous
	 * span at end of group 4). All 4 block bitmaps, 4 inode bitmaps, and
	 * 4*8=32 inode table blocks (40 blocks total) pack into group 4!
	 */
	g4_start = ext4_group_first_block_no(sb, 4);
	KUNIT_EXPECT_EQ(test, ext4_alloc_group_tables_test(sb, flex_gd, 4), 0);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[0].block_bitmap, g4_start);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[3].block_bitmap, g4_start + 3);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[0].inode_bitmap, g4_start + 4);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[3].inode_bitmap, g4_start + 7);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[0].inode_table, g4_start + 8);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[3].inode_table, g4_start + 32);
	/* Group 4 hosts all 40 metadata blocks -> BG_BLOCK_UNINIT cleared */
	KUNIT_EXPECT_EQ(test, flex_gd->bg_flags[0] & EXT4_BG_BLOCK_UNINIT, 0);
	KUNIT_EXPECT_EQ(test, flex_gd->groups[0].free_clusters_count,
			1024U - 40U);

	/* 4. Insufficient space in flex_gd returns -ENOSPC */
	flex_gd->count = 1;
	flex_gd->groups[0].group = 4;
	flex_gd->groups[0].blocks_count = 5;
	flex_gd->groups[0].mdata_blocks = 0;
	KUNIT_EXPECT_EQ(test, ext4_alloc_group_tables_test(sb, flex_gd, 4),
			-ENOSPC);

	ext4_free_flex_gd_test(flex_gd);
}

/*
 * Test 5: ext4_group_extend() validation and partial-last-group online
 * extension updating superblock, block bitmap, buddy cache, and backup SB.
 */
static void test_group_extend_partial_last_group(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_super_block *es = ctx->es;
	struct ext4_super_block *backup_es;
	struct buffer_head *b1_bh;
	ext4_fsblk_t o_blks, o_free;
	s64 free_before;

	/* 2 groups: group 0 full (1024 blks), group 1 partial (600 blks) */
	resize_configure_fs(test, 2, 0, 0, 600);
	o_blks = ext4_blocks_count(es);
	o_free = ext4_free_blocks_count(es);
	KUNIT_ASSERT_EQ(test, o_blks, 1ULL + 1024ULL + 600ULL);

	/* No-op for 0 and o_blocks_count */
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, 0), 0);
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, o_blks), 0);

	/* Shrink attempt -> -EINVAL */
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, o_blks - 10), -EINVAL);

	/* Target beyond underlying device -> -ENOSPC */
	ctx->device_blocks = o_blks + 100;
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, o_blks + 200), -ENOSPC);
	ctx->device_blocks = 1 + 64 * RESIZE_TEST_BLOCKS_PER_GRP;

	/* Extend group 1 by 300 blocks (from 600 to 900 blocks) */
	free_before = percpu_counter_sum(&ctx->sbi.s_freeclusters_counter);
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, o_blks + 300), 0);
	KUNIT_EXPECT_EQ(test, ext4_blocks_count(es), o_blks + 300);
	KUNIT_EXPECT_EQ(test, ext4_free_blocks_count(es), o_free + 300);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_freeclusters_counter),
			free_before + 300);

	/* Extend past group 1 boundary clamps to end of group 1 (+124 blocks) */
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, o_blks + 800), 0);
	KUNIT_EXPECT_EQ(test, ext4_blocks_count(es), 1ULL + 2048ULL);

	/* Once group 1 is full, further ext4_group_extend returns -EPERM */
	KUNIT_EXPECT_EQ(test, ext4_group_extend(sb, es, 1ULL + 2200ULL),
			-EPERM);

	/* Backup superblock in group 1 (block 1025) reflects new size */
	b1_bh = resize_lookup_bh(ctx, ext4_group_first_block_no(sb, 1));
	KUNIT_ASSERT_NOT_NULL(test, b1_bh);
	backup_es = (struct ext4_super_block *)b1_bh->b_data;
	KUNIT_EXPECT_EQ(test, ext4_blocks_count(backup_es), 1ULL + 2048ULL);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(backup_es->s_block_group_nr), 1U);
}

/*
 * Test 6: ext4_group_add() error guards and adding non-sparse group 2 plus
 * sparse backup group 3 (exercising reserve_backup_gdb() on resize_inode).
 */
static void test_group_add_with_resize_inode_and_backup_gdb(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_new_group_data input;
	struct buffer_head *pri_bh;
	__le32 *pri_data;
	blkcnt_t rsv_iblocks_before;
	ext4_fsblk_t start;

	/* 2 initial groups, 1 reserved GDT block (at block 3) */
	resize_configure_fs(test, 2, 1, 0, RESIZE_TEST_BLOCKS_PER_GRP);

	/* 1. Overflow guards on blocks_count and s_inodes_count */
	memset(&input, 0, sizeof(input));
	input.group = 2;
	input.blocks_count = 1024;
	ext4_blocks_count_set(ctx->es, ~0ULL - 100);
	KUNIT_EXPECT_EQ(test, ext4_group_add(sb, &input), -EINVAL);
	ext4_blocks_count_set(ctx->es, 1 + 2 * RESIZE_TEST_BLOCKS_PER_GRP);

	ctx->es->s_inodes_count = cpu_to_le32(~0U - 10);
	KUNIT_EXPECT_EQ(test, ext4_group_add(sb, &input), -EINVAL);
	ctx->es->s_inodes_count = cpu_to_le32(2 * RESIZE_TEST_INODES_PER_GRP);

	/* 2. Add group 2 (no backup super, overhead = 0) */
	start = ext4_blocks_count(ctx->es);
	memset(&input, 0, sizeof(input));
	input.group = 2;
	input.blocks_count = RESIZE_TEST_BLOCKS_PER_GRP;
	input.block_bitmap = start;
	input.inode_bitmap = start + 1;
	input.inode_table = start + 2;

	KUNIT_EXPECT_EQ(test, ext4_group_add(sb, &input), 0);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_groups_count, 3U);

	/* 3. Add group 3 (sparse backup super -> overhead = 3, reserves backup) */
	start = ext4_blocks_count(ctx->es);
	memset(&input, 0, sizeof(input));
	input.group = 3;
	input.blocks_count = RESIZE_TEST_BLOCKS_PER_GRP;
	input.block_bitmap = start + 3;
	input.inode_bitmap = start + 4;
	input.inode_table = start + 5;

	/* Without resize_inode feature -> -EPERM */
	ext4_clear_feature_resize_inode(sb);
	KUNIT_EXPECT_EQ(test, ext4_group_add(sb, &input), -EPERM);
	ext4_set_feature_resize_inode(sb);

	rsv_iblocks_before = ctx->resize_inode->i_blocks;
	KUNIT_EXPECT_EQ(test, ext4_group_add(sb, &input), 0);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_groups_count, 4U);

	/*
	 * reserve_backup_gdb() must append group 3's reserved GDT block
	 * (3 * 1024 + 3 = 3075) at index 1 (after group 1's backup) in
	 * primary reserved GDT block 3 and increment resize_inode->i_blocks.
	 */
	pri_bh = resize_lookup_bh(ctx, 3);
	KUNIT_ASSERT_NOT_NULL(test, pri_bh);
	pri_data = (__le32 *)pri_bh->b_data;
	KUNIT_EXPECT_EQ(test, le32_to_cpu(pri_data[1]), 3U * 1024U + 3U);
	KUNIT_EXPECT_EQ(test, ctx->resize_inode->i_blocks,
			rsv_iblocks_before + (RESIZE_TEST_BLK_SIZE >> 9));
}

/*
 * Test 7: ext4_convert_meta_bg() sanity validation of resize_inode and
 * conversion from resize_inode to meta_bg.
 */
static void test_convert_meta_bg_validation_and_conversion(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *rinode;
	ext4_fsblk_t dind_blk = 42;

	resize_configure_fs(test, 16, 0, 0, RESIZE_TEST_BLOCKS_PER_GRP);
	ext4_set_feature_resize_inode(sb);

	rinode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, rinode);
	rinode->i_blkbits = RESIZE_TEST_BLK_BITS;
	rinode->i_blocks = RESIZE_TEST_BLK_SIZE >> 9;
	EXT4_I(rinode)->i_data[EXT4_DIND_BLOCK] = cpu_to_le32(dind_blk);

	/* 1. Non-zero s_reserved_gdt_blocks -> -EPERM */
	ctx->es->s_reserved_gdt_blocks = cpu_to_le16(1);
	KUNIT_EXPECT_EQ(test, ext4_convert_meta_bg_test(sb, rinode), -EPERM);
	ctx->es->s_reserved_gdt_blocks = 0;

	/* 2. Wrong i_blocks on resize_inode -> -EINVAL */
	rinode->i_blocks = 8;
	KUNIT_EXPECT_EQ(test, ext4_convert_meta_bg_test(sb, rinode), -EINVAL);
	rinode->i_blocks = RESIZE_TEST_BLK_SIZE >> 9;

	/* 3. Unexpected direct block pointer or missing DIND -> -EINVAL */
	EXT4_I(rinode)->i_data[0] = cpu_to_le32(10);
	KUNIT_EXPECT_EQ(test, ext4_convert_meta_bg_test(sb, rinode), -EINVAL);
	EXT4_I(rinode)->i_data[0] = 0;

	EXT4_I(rinode)->i_data[EXT4_DIND_BLOCK] = 0;
	KUNIT_EXPECT_EQ(test, ext4_convert_meta_bg_test(sb, rinode), -EINVAL);
	EXT4_I(rinode)->i_data[EXT4_DIND_BLOCK] = cpu_to_le32(dind_blk);
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;

	/* 4. Valid conversion clears RESIZE_INODE, sets META_BG, frees DIND */
	KUNIT_EXPECT_EQ(test, ext4_convert_meta_bg_test(sb, rinode), 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_feature_resize_inode(sb));
	KUNIT_EXPECT_TRUE(test, ext4_has_feature_meta_bg(sb));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es->s_first_meta_bg), 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_freed_blk, dind_blk);
	KUNIT_EXPECT_EQ(test, ctx->last_freed_count, 1UL);
	KUNIT_EXPECT_EQ(test, rinode->i_blocks, 0ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(rinode)->i_data[EXT4_DIND_BLOCK]),
			0U);

	iput(rinode);
}

/*
 * Test 8: End-to-end ext4_resize_fs() across error guards, bigalloc cluster
 * rounding, consuming reserved GDT blocks (16 -> 32 groups via add_new_gdb),
 * converting resize_inode -> meta_bg, and growing into meta_bg (32 -> 34
 * groups via add_new_gdb_meta_bg).
 */
static void test_resize_fs_across_resize_inode_to_meta_bg(struct kunit *test)
{
	struct resize_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	ext4_fsblk_t o_blks, target_blks, dind_blk;

	/*
	 * Start with 16 groups (1 full 64-byte GDT block), 1 reserved GDT
	 * block (allowing growth to 32 groups before meta_bg conversion), and
	 * flex_bg of 4 groups (log_flex = 2).
	 */
	resize_configure_fs(test, 16, 1, 2, RESIZE_TEST_BLOCKS_PER_GRP);
	o_blks = ext4_blocks_count(ctx->es);
	dind_blk = le32_to_cpu(
		EXT4_I(ctx->resize_inode)->i_data[EXT4_DIND_BLOCK]);

	/* 1. Beyond device -> -ENOSPC; shrink -> -EINVAL; same size -> 0 */
	KUNIT_EXPECT_EQ(test, ext4_resize_fs(sb, ctx->device_blocks + 100),
			-ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_resize_fs(sb, o_blks - 100), -EINVAL);
	KUNIT_EXPECT_EQ(test, ext4_resize_fs(sb, o_blks), 0);

	/* 2. Simultaneous resize_inode + meta_bg -> -EINVAL */
	ext4_set_feature_meta_bg(sb);
	KUNIT_EXPECT_EQ(test, ext4_resize_fs(sb, o_blks + 1024), -EINVAL);
	ext4_clear_feature_meta_bg(sb);
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;

	/* 3. Grow from 16 groups to 34 groups across GDT & meta_bg boundary */
	target_blks = RESIZE_TEST_FIRST_DATA_BLK +
		      34ULL * RESIZE_TEST_BLOCKS_PER_GRP;
	KUNIT_EXPECT_EQ(test, ext4_resize_fs(sb, target_blks), 0);

	KUNIT_EXPECT_EQ(test, ctx->sbi.s_groups_count, 34U);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_gdb_count, 3UL);
	KUNIT_EXPECT_EQ(test, ext4_blocks_count(ctx->es), target_blks);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ctx->es->s_reserved_gdt_blocks), 0U);
	KUNIT_EXPECT_FALSE(test, ext4_has_feature_resize_inode(sb));
	KUNIT_EXPECT_TRUE(test, ext4_has_feature_meta_bg(sb));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es->s_first_meta_bg), 2U);
	KUNIT_EXPECT_EQ(test, ctx->last_freed_blk, dind_blk);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es->s_inodes_count),
			34U * RESIZE_TEST_INODES_PER_GRP);
}

static struct kunit_case ext4_resize_test_cases[] = {
	KUNIT_CASE(test_resize_begin_and_end_guards),
	KUNIT_CASE(test_list_backups_and_verify_reserved_gdb),
	KUNIT_CASE(test_verify_group_input_rejections_and_acceptance),
	KUNIT_CASE(test_alloc_setup_flex_gd_and_group_tables),
	KUNIT_CASE(test_group_extend_partial_last_group),
	KUNIT_CASE(test_group_add_with_resize_inode_and_backup_gdb),
	KUNIT_CASE(test_convert_meta_bg_validation_and_conversion),
	KUNIT_CASE(test_resize_fs_across_resize_inode_to_meta_bg),
	{}
};

static struct kunit_suite ext4_resize_test_suite = {
	.name = "ext4_resize_test",
	.init = resize_test_init,
	.test_cases = ext4_resize_test_cases,
};

kunit_test_suites(&ext4_resize_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 online filesystem resizing");
MODULE_LICENSE("GPL");
