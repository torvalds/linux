// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/indirect.c
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fscrypt.h>
#include <linux/iversion.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/percpu-rwsem.h>
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

#define IND_TEST_BLK_SIZE		1024U
#define IND_TEST_BLK_BITS		10U
#define IND_TEST_PTRS_PER_BLK		(IND_TEST_BLK_SIZE / sizeof(__le32))
#define IND_TEST_BLOCKS_PER_GRP		1024U
#define IND_TEST_NGROUPS		8U
#define IND_TEST_FIRST_DATA_BLK		1U
#define IND_TEST_TOTAL_BLKS		(IND_TEST_FIRST_DATA_BLK + \
					 IND_TEST_NGROUPS * \
					 IND_TEST_BLOCKS_PER_GRP)
#define IND_TEST_INODE_SIZE		256U
#define IND_TEST_EXTRA_ISIZE		32U
#define IND_TEST_INODES_PER_GRP		32U
#define IND_TEST_MAX_FREED_REC		128U

#define IND_TEST_IND_BASE		EXT4_NDIR_BLOCKS
#define IND_TEST_DIND_BASE		(IND_TEST_IND_BASE + \
					 IND_TEST_PTRS_PER_BLK)
#define IND_TEST_TIND_BASE		(IND_TEST_DIND_BASE + \
					 IND_TEST_PTRS_PER_BLK * \
					 IND_TEST_PTRS_PER_BLK)
#define IND_TEST_MAX_LBLK		(IND_TEST_TIND_BASE + \
					 IND_TEST_PTRS_PER_BLK * \
					 IND_TEST_PTRS_PER_BLK * \
					 IND_TEST_PTRS_PER_BLK)

struct ind_mock_blk {
	struct list_head node;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	u8 *data;
};

struct ind_test_ctx {
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
	ext4_fsblk_t device_blocks;
	ext4_fsblk_t next_alloc_pblk;
	unsigned int alloc_calls;
	unsigned int fail_alloc_on_call;
	int fail_alloc_err;
	int read_bh_err;
	ext4_fsblk_t freed_blks[IND_TEST_MAX_FREED_REC];
	unsigned int freed_cnt;
	bool rwsem_inited;
	bool es_shrinker_inited;
};

static struct kunit *active_ind_test;

static struct inode *ind_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = IND_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void ind_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void ind_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **ind_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations ind_test_sops = {
	.alloc_inode	= ind_test_alloc_inode,
	.free_inode	= ind_test_free_inode,
	.evict_inode	= ind_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= ind_test_get_dquots,
#endif
};

static int ind_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static void ind_test_sb_upd_work(struct work_struct *work)
{
}

static void ind_test_err_timer(struct timer_list *t)
{
}

static struct file_system_type ind_test_fs_type = {
	.name		 = "ext4_indirect_kunit",
	.init_fs_context = ind_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void ind_init_mock_bh(struct buffer_head *bh, struct folio *folio,
			     u8 *data, size_t size, sector_t blocknr)
{
	memset(bh, 0, sizeof(*bh));
	bh->b_folio = folio;
	bh->b_data = (char *)data;
	bh->b_size = size;
	bh->b_blocknr = blocknr;
	atomic_set(&bh->b_count, 2);
	set_buffer_uptodate(bh);
	set_buffer_mapped(bh);
}

static struct buffer_head *ind_lookup_bh(struct ind_test_ctx *ctx,
					 ext4_fsblk_t blocknr)
{
	struct ind_mock_blk *blk;

	list_for_each_entry(blk, &ctx->blks, node) {
		if (blk->blocknr == blocknr)
			return &blk->bh;
	}
	return NULL;
}

static struct buffer_head *ind_get_or_create_bh(struct ind_test_ctx *ctx,
						ext4_fsblk_t blocknr)
{
	struct buffer_head *bh;
	struct ind_mock_blk *blk;

	bh = ind_lookup_bh(ctx, blocknr);
	if (bh) {
		get_bh(bh);
		return bh;
	}

	blk = kzalloc_obj(*blk);
	if (!blk)
		return NULL;
	blk->data = kzalloc(IND_TEST_BLK_SIZE, GFP_KERNEL);
	if (!blk->data) {
		kfree(blk);
		return NULL;
	}
	blk->blocknr = blocknr;
	ind_init_mock_bh(&blk->bh, ctx->bh_folio, blk->data,
			 IND_TEST_BLK_SIZE, blocknr);
	list_add_tail(&blk->node, &ctx->blks);
	get_bh(&blk->bh);
	return &blk->bh;
}

static struct buffer_head *ind_sb_getblk_stub(struct super_block *sb,
					      ext4_fsblk_t block)
{
	struct kunit *test = active_ind_test;
	struct ind_test_ctx *ctx;

	if (!test)
		return NULL;
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return NULL;

	return ind_get_or_create_bh(ctx, block);
}

static struct buffer_head *ind_sb_bread_nofail_stub(struct super_block *sb,
						    sector_t block)
{
	struct kunit *test = active_ind_test;
	struct ind_test_ctx *ctx;
	struct buffer_head *bh;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return ERR_PTR(-EIO);
	if ((ext4_fsblk_t)block >= ctx->device_blocks)
		return ERR_PTR(-EIO);

	bh = ind_get_or_create_bh(ctx, block);
	if (!bh)
		return ERR_PTR(-ENOMEM);
	return bh;
}

static int ind_read_bh_stub(struct buffer_head *bh, blk_opf_t op_flags,
			    bio_end_io_t end_io, bool simu_fail)
{
	struct kunit *test = active_ind_test;
	struct ind_test_ctx *ctx;

	if (!test) {
		unlock_buffer(bh);
		return -EIO;
	}
	ctx = test->priv;
	if (ctx->read_bh_err) {
		unlock_buffer(bh);
		return ctx->read_bh_err;
	}

	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	return 0;
}

static int ind_handle_dirty_metadata_stub(const char *where, unsigned int line,
					  handle_t *handle, struct inode *inode,
					  struct buffer_head *bh)
{
	if (bh)
		set_buffer_uptodate(bh);
	return 0;
}

static int ind_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
				     const char *func, unsigned int line)
{
	return 0;
}

static ext4_fsblk_t ind_mb_new_blocks_stub(handle_t *handle,
					   struct ext4_allocation_request *ar,
					   int *errp)
{
	struct kunit *test = active_ind_test;
	struct ind_test_ctx *ctx;
	ext4_fsblk_t pblk;

	if (!test) {
		*errp = -EIO;
		return 0;
	}
	ctx = test->priv;
	ctx->alloc_calls++;
	if (ctx->fail_alloc_on_call &&
	    ctx->alloc_calls == ctx->fail_alloc_on_call) {
		*errp = ctx->fail_alloc_err;
		return 0;
	}

	pblk = ctx->next_alloc_pblk;
	ctx->next_alloc_pblk += ar->len;
	ar->inode->i_blocks += (blkcnt_t)ar->len <<
			       (ar->inode->i_blkbits - 9);
	*errp = 0;
	return pblk;
}

static void ind_free_blocks_stub(handle_t *handle, struct inode *inode,
				 struct buffer_head *bh, ext4_fsblk_t block,
				 unsigned long count, int flags)
{
	struct kunit *test = active_ind_test;
	struct ind_test_ctx *ctx;
	ext4_fsblk_t start_blk = block;
	blkcnt_t sectors;
	unsigned long i;

	if (!test)
		return;
	ctx = test->priv;
	if (!start_blk && bh)
		start_blk = bh->b_blocknr;

	for (i = 0; i < count && ctx->freed_cnt < IND_TEST_MAX_FREED_REC; i++)
		ctx->freed_blks[ctx->freed_cnt++] = start_blk + i;

	sectors = (blkcnt_t)count << (inode->i_blkbits - 9);
	if (inode->i_blocks >= sectors)
		inode->i_blocks -= sectors;
	if (bh)
		brelse(bh);
}

static bool ind_was_freed(struct ind_test_ctx *ctx, ext4_fsblk_t blk)
{
	unsigned int i;

	for (i = 0; i < ctx->freed_cnt; i++) {
		if (ctx->freed_blks[i] == blk)
			return true;
	}
	return false;
}

static __le32 *ind_blk_ptrs(struct kunit *test, ext4_fsblk_t blk)
{
	struct ind_test_ctx *ctx = test->priv;
	struct buffer_head *bh;

	bh = ind_get_or_create_bh(ctx, blk);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	brelse(bh);
	return (__le32 *)bh->b_data;
}

static struct inode *ind_make_test_inode(struct kunit *test, unsigned long ino)
{
	struct ind_test_ctx *ctx = test->priv;
	struct inode *inode;

	inode = new_inode(ctx->sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);
	inode->i_ino = ino;
	inode->i_mode = S_IFREG | 0644;
	inode->i_blkbits = IND_TEST_BLK_BITS;
	inode->i_flags |= S_NOQUOTA;
	EXT4_I(inode)->i_block_group = 0;
	return inode;
}

static void ind_test_cleanup(struct ind_test_ctx *ctx)
{
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ind_mock_blk *blk, *tmp;

	active_ind_test = NULL;

	cancel_work_sync(&sbi->s_sb_upd_work);
	timer_delete_sync(&sbi->s_err_report);

	if (sb) {
		deactivate_super(sb);
		ctx->sb = NULL;
	}
	rcu_barrier();

	if (ctx->es_shrinker_inited) {
		ext4_es_unregister_shrinker(sbi);
		ext4_es_destroy_stats(sbi);
		ctx->es_shrinker_inited = false;
	}

	percpu_counter_destroy(&sbi->s_freeclusters_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);

	if (ctx->rwsem_inited) {
		percpu_free_rwsem(&sbi->s_writepages_rwsem);
		ctx->rwsem_inited = false;
	}

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

KUNIT_DEFINE_ACTION_WRAPPER(ind_test_cleanup_action, ind_test_cleanup,
			    struct ind_test_ctx *);

static int ind_test_set_super(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static int ind_test_init(struct kunit *test)
{
	struct ind_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct buffer_head *sbh;
	struct inode *root_inode;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	INIT_LIST_HEAD(&ctx->blks);
	ctx->device_blocks = IND_TEST_TOTAL_BLKS;
	ctx->next_alloc_pblk = 1000;
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	KUNIT_ASSERT_TRUE(test, ctx->bdev.bd_stats != NULL);

	ctx->bh_folio = folio_alloc(GFP_KERNEL, 0);
	if (!ctx->bh_folio) {
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "folio_alloc failed");
	}

	fc = fs_context_for_mount(&ind_test_fs_type, 0);
	if (IS_ERR(fc)) {
		folio_put(ctx->bh_folio);
		ctx->bh_folio = NULL;
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "fs_context_for_mount failed: %ld",
				     PTR_ERR(fc));
	}
	sb = sget_fc(fc, NULL, ind_test_set_super);
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
	INIT_WORK(&sbi->s_sb_upd_work, ind_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, ind_test_err_timer, 0);
	up_write(&sb->s_umount);

	ret = kunit_add_action_or_reset(test, ind_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sb->s_fs_info = sbi;
	sb->s_op = &ind_test_sops;
	sb->s_blocksize = IND_TEST_BLK_SIZE;
	sb->s_blocksize_bits = IND_TEST_BLK_BITS;
	sb->s_magic = EXT4_SUPER_MAGIC;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	INIT_LIST_HEAD(&sb->s_inodes);
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif

	strscpy(sb->s_id, "ext4_indirect_kunit", sizeof(sb->s_id));
	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = IND_TEST_BLK_BITS;
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

	ret = percpu_init_rwsem(&sbi->s_writepages_rwsem);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->rwsem_inited = true;

	ret = percpu_counter_init(&sbi->s_freeclusters_counter, 4096,
				  GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = percpu_counter_init(&sbi->s_freeinodes_counter, 200, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = ext4_es_register_shrinker(sbi);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->es_shrinker_inited = true;

	sbi->s_addr_per_block_bits = ilog2(EXT4_ADDR_PER_BLOCK(sb));
	sbi->s_blocks_per_group = IND_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = IND_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = IND_TEST_NGROUPS;
	sbi->s_inodes_per_group = IND_TEST_INODES_PER_GRP;
	sbi->s_inode_size = IND_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = IND_TEST_BLK_SIZE / IND_TEST_INODE_SIZE;
	sbi->s_first_ino = 11;
	sbi->s_mount_state = EXT4_VALID_FS;
	sbi->s_bitmap_maxbytes = ext4_max_bitmap_size_test(IND_TEST_BLK_BITS,
							   1);
	set_opt(sb, ERRORS_CONT);

	sbh = ind_get_or_create_bh(ctx, IND_TEST_FIRST_DATA_BLK);
	KUNIT_ASSERT_NOT_NULL(test, sbh);
	clear_buffer_mapped(sbh);
	brelse(sbh);
	sbi->s_sbh = sbh;
	ctx->es = (struct ext4_super_block *)sbh->b_data;
	sbi->s_es = ctx->es;

	ctx->es->s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es->s_first_data_block = cpu_to_le32(IND_TEST_FIRST_DATA_BLK);
	ctx->es->s_blocks_per_group = cpu_to_le32(IND_TEST_BLOCKS_PER_GRP);
	ctx->es->s_clusters_per_group = cpu_to_le32(IND_TEST_BLOCKS_PER_GRP);
	ctx->es->s_inodes_per_group = cpu_to_le32(IND_TEST_INODES_PER_GRP);
	ctx->es->s_inodes_count = cpu_to_le32(IND_TEST_NGROUPS *
					      IND_TEST_INODES_PER_GRP);
	ctx->es->s_inode_size = cpu_to_le16(IND_TEST_INODE_SIZE);
	ctx->es->s_checksum_type = EXT4_CRC32C_CHKSUM;
	generate_random_uuid(ctx->es->s_uuid);
	ext4_blocks_count_set(ctx->es, IND_TEST_TOTAL_BLKS);
	ext4_set_feature_64bit(sb);
	ext4_set_feature_metadata_csum(sb);
	sbi->s_csum_seed = ext4_chksum(~0, ctx->es->s_uuid,
				       sizeof(ctx->es->s_uuid));
	ext4_superblock_csum_set(sb);

	root_inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, root_inode);
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_blkbits = IND_TEST_BLK_BITS;
	root_inode->i_flags |= S_NOQUOTA;
	root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	KUNIT_ASSERT_NOT_NULL(test, sb->s_root);
	sb->s_flags |= SB_ACTIVE;

	active_ind_test = test;
	kunit_activate_static_stub(test, ext4_sb_getblk, ind_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_sb_bread_nofail,
				   ind_sb_bread_nofail_stub);
	kunit_activate_static_stub(test, ext4_read_bh, ind_read_bh_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   ind_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   ind_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   ind_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   ind_free_blocks_stub);

	return 0;
}

/*
 * Test 1: ext4_block_to_path() across direct, single-, double-, and
 * triple-indirect boundaries plus out-of-range block rejection, and
 * ext4_ind_trans_blocks() credit estimation.
 */
static void test_block_to_path_and_trans_blocks(struct kunit *test)
{
	struct inode *inode = ind_make_test_inode(test, 12);
	ext4_lblk_t offsets[4];
	int boundary = -1;

	/* Direct: block 0 and block 11 (boundary = 0) */
	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, 0, offsets, &boundary),
			1);
	KUNIT_EXPECT_EQ(test, offsets[0], 0U);
	KUNIT_EXPECT_EQ(test, boundary, 11);

	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, 11, offsets, &boundary),
			1);
	KUNIT_EXPECT_EQ(test, offsets[0], 11U);
	KUNIT_EXPECT_EQ(test, boundary, 0);

	/* Single-indirect: first (12) and last (267) */
	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_IND_BASE,
						offsets, &boundary), 2);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_IND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 0U);
	KUNIT_EXPECT_EQ(test, boundary, 255);

	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_DIND_BASE - 1,
						offsets, &boundary), 2);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_IND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 255U);
	KUNIT_EXPECT_EQ(test, boundary, 0);

	/* Double-indirect: first (268), end of first IND (523), last (65803) */
	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_DIND_BASE,
						offsets, &boundary), 3);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_DIND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 0U);
	KUNIT_EXPECT_EQ(test, offsets[2], 0U);
	KUNIT_EXPECT_EQ(test, boundary, 255);

	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_DIND_BASE + 255,
						offsets, &boundary), 3);
	KUNIT_EXPECT_EQ(test, offsets[1], 0U);
	KUNIT_EXPECT_EQ(test, offsets[2], 255U);
	KUNIT_EXPECT_EQ(test, boundary, 0);

	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_TIND_BASE - 1,
						offsets, &boundary), 3);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_DIND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 255U);
	KUNIT_EXPECT_EQ(test, offsets[2], 255U);
	KUNIT_EXPECT_EQ(test, boundary, 0);

	/* Triple-indirect: first (65804) and last (16843019) */
	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_TIND_BASE,
						offsets, &boundary), 4);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_TIND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 0U);
	KUNIT_EXPECT_EQ(test, offsets[2], 0U);
	KUNIT_EXPECT_EQ(test, offsets[3], 0U);
	KUNIT_EXPECT_EQ(test, boundary, 255);

	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_MAX_LBLK - 1,
						offsets, NULL), 4);
	KUNIT_EXPECT_EQ(test, offsets[0], (ext4_lblk_t)EXT4_TIND_BLOCK);
	KUNIT_EXPECT_EQ(test, offsets[1], 255U);
	KUNIT_EXPECT_EQ(test, offsets[2], 255U);
	KUNIT_EXPECT_EQ(test, offsets[3], 255U);

	/* Out-of-range block returns 0 */
	KUNIT_EXPECT_EQ(test,
			ext4_block_to_path_test(inode, IND_TEST_MAX_LBLK,
						offsets, NULL), 0);

	/* ext4_ind_trans_blocks(): DIV_ROUND_UP(nrblocks, 256) + 4 */
	KUNIT_EXPECT_EQ(test, ext4_ind_trans_blocks(inode, 1), 5);
	KUNIT_EXPECT_EQ(test, ext4_ind_trans_blocks(inode, 256), 5);
	KUNIT_EXPECT_EQ(test, ext4_ind_trans_blocks(inode, 257), 6);
	KUNIT_EXPECT_EQ(test, ext4_ind_trans_blocks(inode, 512), 6);

	iput(inode);
}

/*
 * Test 2: ext4_ind_map_blocks() read-only lookup for contiguous mapped runs,
 * EXT4_MAP_BOUNDARY detection, and multi-level unallocated hole span sizing.
 */
static void test_ind_map_blocks_lookup_mapped_and_holes(struct kunit *test)
{
	struct inode *inode = ind_make_test_inode(test, 13);
	struct ext4_map_blocks map;
	unsigned int i;
	int ret;

	/* Map direct [0..3] -> [100..103], hole at [4], [5..11] -> [205..211] */
	for (i = 0; i < 4; i++)
		EXT4_I(inode)->i_data[i] = cpu_to_le32(100 + i);
	for (i = 5; i < EXT4_NDIR_BLOCKS; i++)
		EXT4_I(inode)->i_data[i] = cpu_to_le32(200 + i);

	/* Contiguous direct run [0..3] stops at hole [4], not at boundary */
	memset(&map, 0, sizeof(map));
	map.m_lblk = 0;
	map.m_len = 10;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 4);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 100ULL);
	KUNIT_EXPECT_EQ(test, map.m_len, 4U);
	KUNIT_EXPECT_TRUE(test, map.m_flags & EXT4_MAP_MAPPED);
	KUNIT_EXPECT_FALSE(test, map.m_flags & EXT4_MAP_BOUNDARY);

	/* Contiguous direct run [5..11] hits end of direct array -> BOUNDARY */
	memset(&map, 0, sizeof(map));
	map.m_lblk = 5;
	map.m_len = 10;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 7);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 205ULL);
	KUNIT_EXPECT_EQ(test, map.m_len, 7U);
	KUNIT_EXPECT_EQ(test, map.m_flags & (EXT4_MAP_MAPPED | EXT4_MAP_BOUNDARY),
			(unsigned int)(EXT4_MAP_MAPPED | EXT4_MAP_BOUNDARY));

	/* Direct hole at [4]: hole length is 1 */
	memset(&map, 0, sizeof(map));
	map.m_lblk = 4;
	map.m_len = 5;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 0ULL);
	KUNIT_EXPECT_EQ(test, map.m_len, 1U);

	/* Absent IND subtree at lblk = 20 (offsets = [12, 8]) -> 256 - 8 = 248 */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_IND_BASE + 8;
	map.m_len = 500;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_len, 248U);

	/*
	 * Absent DIND subtree at offsets = [13, 2, 10]:
	 * remaining blocks in DIND = (255 - 2) * 256 + (256 - 10) = 65014.
	 */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_DIND_BASE + 2 * IND_TEST_PTRS_PER_BLK + 10;
	map.m_len = 100000;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_len, 65014U);

	iput(inode);
}

/*
 * Test 3: ext4_ind_map_blocks() block allocation and branch splicing across
 * direct, single-indirect, double-indirect, and triple-indirect levels.
 */
static void test_ind_map_blocks_alloc_direct_ind_dind_tind(struct kunit *test)
{
	struct inode *inode = ind_make_test_inode(test, 14);
	struct ext4_map_blocks map, check;
	handle_t *handle;
	int ret;

	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 8);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));

	/* 1. Multi-block direct allocation at [2..5] */
	memset(&map, 0, sizeof(map));
	map.m_lblk = 2;
	map.m_len = 4;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 4);
	KUNIT_EXPECT_EQ(test, map.m_flags & (EXT4_MAP_NEW | EXT4_MAP_MAPPED),
			(unsigned int)(EXT4_MAP_NEW | EXT4_MAP_MAPPED));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[1]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[2]),
			(u32)map.m_pblk);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[5]),
			(u32)(map.m_pblk + 3));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[6]), 0U);

	/* 2. Direct allocation clamped at direct boundary [9..11] */
	memset(&map, 0, sizeof(map));
	map.m_lblk = 9;
	map.m_len = 6;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 3);
	KUNIT_EXPECT_TRUE(test, map.m_flags & EXT4_MAP_BOUNDARY);

	/* 3. Single-indirect allocation at [15..19] */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_IND_BASE + 3;
	map.m_len = 5;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_NE(test, le32_to_cpu(EXT4_I(inode)->i_data[EXT4_IND_BLOCK]),
			0U);

	memset(&check, 0, sizeof(check));
	check.m_lblk = IND_TEST_IND_BASE + 3;
	check.m_len = 10;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &check, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_EQ(test, check.m_pblk, map.m_pblk);

	/* 4. Double-indirect allocation at DIND_BASE + 300 (len = 3) */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_DIND_BASE + 300;
	map.m_len = 3;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 3);
	KUNIT_EXPECT_NE(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_DIND_BLOCK]),
			0U);

	memset(&check, 0, sizeof(check));
	check.m_lblk = IND_TEST_DIND_BASE + 300;
	check.m_len = 5;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &check, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 3);
	KUNIT_EXPECT_EQ(test, check.m_pblk, map.m_pblk);

	/* 5. Triple-indirect allocation at TIND_BASE + 5 (len = 2) */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_TIND_BASE + 5;
	map.m_len = 2;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_NE(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_TIND_BLOCK]),
			0U);

	memset(&check, 0, sizeof(check));
	check.m_lblk = IND_TEST_TIND_BASE + 5;
	check.m_len = 4;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &check, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, check.m_pblk, map.m_pblk);

	ext4_journal_stop(handle);
	iput(inode);
}

/*
 * Test 4: ext4_ind_map_blocks() error paths and partial branch unwind in
 * ext4_alloc_branch() when an intermediate indirect or leaf allocation fails.
 */
static void test_ind_map_blocks_errors_and_alloc_branch_unwind(struct kunit *test)
{
	struct ind_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode = ind_make_test_inode(test, 15);
	struct ext4_map_blocks map;
	struct buffer_head *ind_bh;
	ext4_fsblk_t alloc_base;
	handle_t *handle;
	int ret;

	handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS, 8);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));

	/* 1. Out-of-range m_lblk -> -EIO */
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_MAX_LBLK;
	map.m_len = 1;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -EIO);

	/* 2. Allocating on an indirect inode with bigalloc -> -EFSCORRUPTED */
	ext4_set_feature_bigalloc(sb);
	memset(&map, 0, sizeof(map));
	map.m_lblk = 0;
	map.m_len = 1;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	ext4_clear_feature_bigalloc(sb);

	/* 3. Out-of-range indirect block pointer on lookup -> -EFSCORRUPTED */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] =
		cpu_to_le32(IND_TEST_TOTAL_BLKS + 10);
	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_IND_BASE;
	map.m_len = 1;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);

	/* 4. Non-uptodate IND block read I/O failure -> -EIO */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(300);
	ind_bh = ind_get_or_create_bh(ctx, 300);
	KUNIT_ASSERT_NOT_NULL(test, ind_bh);
	clear_buffer_uptodate(ind_bh);
	ctx->read_bh_err = -EIO;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	ctx->read_bh_err = 0;

	/*
	 * 5. Non-uptodate IND block containing invalid blockref (block 1 ==
	 * s_first_data_block) fails ext4_check_indirect_blockref() -> -EIO.
	 */
	((__le32 *)ind_bh->b_data)[0] = cpu_to_le32(IND_TEST_FIRST_DATA_BLK);
	clear_buffer_uptodate(ind_bh);
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	brelse(ind_bh);
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = 0;

	/*
	 * 6. Partial TIND branch unwind: TIND (call 1), DIND (call 2), and
	 * IND (call 3) metadata blocks succeed; leaf data allocation (call 4)
	 * fails with -ENOSPC. All 3 metadata blocks must be freed and
	 * i_data[EXT4_TIND_BLOCK] must remain 0.
	 */
	ctx->freed_cnt = 0;
	ctx->alloc_calls = 0;
	ctx->fail_alloc_on_call = 4;
	ctx->fail_alloc_err = -ENOSPC;
	alloc_base = ctx->next_alloc_pblk;

	memset(&map, 0, sizeof(map));
	map.m_lblk = IND_TEST_TIND_BASE + 9;
	map.m_len = 2;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_TIND_BLOCK]),
			0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, alloc_base));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, alloc_base + 1));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, alloc_base + 2));
	KUNIT_EXPECT_EQ(test, inode->i_blocks, 0ULL);

	ctx->fail_alloc_on_call = 0;
	ctx->fail_alloc_err = 0;
	ext4_journal_stop(handle);
	iput(inode);
}

/*
 * Test 5: ext4_ind_truncate() partial shared-branch truncation inside DIND,
 * boundary truncation detaching IND, and full truncation to i_size = 0.
 */
static void test_ind_truncate_partial_and_full(struct kunit *test)
{
	struct ind_test_ctx *ctx = test->priv;
	struct inode *inode = ind_make_test_inode(test, 16);
	__le32 *ind, *dind, *dind_c0, *dind_c1, *tind, *tind_d0, *tind_i0;
	handle_t *handle;
	unsigned int i;

	/* Direct [0..11] -> [100..111] */
	for (i = 0; i < EXT4_NDIR_BLOCKS; i++)
		EXT4_I(inode)->i_data[i] = cpu_to_le32(100 + i);

	/* IND (500): [0..5] -> [600..605] */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(500);
	ind = ind_blk_ptrs(test, 500);
	for (i = 0; i < 6; i++)
		ind[i] = cpu_to_le32(600 + i);

	/* DIND (501): child0 (502) -> [700..705], child1 (503) -> [710..713] */
	EXT4_I(inode)->i_data[EXT4_DIND_BLOCK] = cpu_to_le32(501);
	dind = ind_blk_ptrs(test, 501);
	dind[0] = cpu_to_le32(502);
	dind[1] = cpu_to_le32(503);
	dind_c0 = ind_blk_ptrs(test, 502);
	for (i = 0; i < 6; i++)
		dind_c0[i] = cpu_to_le32(700 + i);
	dind_c1 = ind_blk_ptrs(test, 503);
	for (i = 0; i < 4; i++)
		dind_c1[i] = cpu_to_le32(710 + i);

	/* TIND (504) -> DIND (505) -> IND (506) -> data (800) */
	EXT4_I(inode)->i_data[EXT4_TIND_BLOCK] = cpu_to_le32(504);
	tind = ind_blk_ptrs(test, 504);
	tind[0] = cpu_to_le32(505);
	tind_d0 = ind_blk_ptrs(test, 505);
	tind_d0[0] = cpu_to_le32(506);
	tind_i0 = ind_blk_ptrs(test, 506);
	tind_i0[0] = cpu_to_le32(800);

	ext4_es_insert_extent(inode, 0, 100000, 100, EXTENT_STATUS_WRITTEN, 0);

	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, 8);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));

	/*
	 * Step A: Truncate to last_block = DIND_BASE + 3.
	 * DIND[0][0..2] (700..702) survive; DIND[0][3..5] (703..705),
	 * DIND[1] (503, 710..713), and TIND (504, 505, 506, 800) are freed.
	 */
	i_size_write(inode, (loff_t)(IND_TEST_DIND_BASE + 3) *
			    IND_TEST_BLK_SIZE);
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_ind_truncate(handle, inode);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, i_size_read(inode));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[EXT4_DIND_BLOCK]),
			501U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(dind[0]), 502U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(dind[1]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(dind_c0[2]), 702U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(dind_c0[3]), 0U);
	KUNIT_EXPECT_FALSE(test, ind_was_freed(ctx, 702));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 703));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 705));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 503));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 710));
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_TIND_BLOCK]),
			0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 504));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 506));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 800));

	/*
	 * Step B: Truncate to last_block = IND_BASE (12).
	 * Direct [0..11] survive; IND (500) and remaining DIND (501, 502)
	 * are detached and freed.
	 */
	ctx->freed_cnt = 0;
	i_size_write(inode, (loff_t)IND_TEST_IND_BASE * IND_TEST_BLK_SIZE);
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_ind_truncate(handle, inode);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_IND_BLOCK]), 0U);
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_DIND_BLOCK]),
			0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 500));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 600));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 501));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 502));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[11]), 111U);

	/* Step C: Truncate to 0 frees all remaining direct blocks */
	ctx->freed_cnt = 0;
	i_size_write(inode, 0);
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_ind_truncate(handle, inode);
	up_write(&EXT4_I(inode)->i_data_sem);

	for (i = 0; i < EXT4_NDIR_BLOCKS; i++)
		KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[i]),
				0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 100));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 111));

	ext4_journal_stop(handle);
	iput(inode);
}

/*
 * Test 6: ext4_ind_remove_space() hole punching within direct blocks, across
 * different tree levels (n < n2), and within the same indirect level (n == n2)
 * converging on a shared IND or DIND block.
 */
static void test_ind_remove_space_levels_and_convergence(struct kunit *test)
{
	struct ind_test_ctx *ctx = test->priv;
	struct inode *inode = ind_make_test_inode(test, 17);
	__le32 *ind, *dind, *c0, *c1, *c2;
	handle_t *handle;
	unsigned int i;
	int ret;

	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, 8);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));

	/* Empty/out-of-range ranges are no-ops */
	down_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ext4_ind_remove_space(handle, inode, 5, 5), 0);
	KUNIT_EXPECT_EQ(test,
			ext4_ind_remove_space(handle, inode,
					      IND_TEST_MAX_LBLK + 10,
					      IND_TEST_MAX_LBLK + 20), 0);
	up_write(&EXT4_I(inode)->i_data_sem);

	/* Populate direct [0..11], IND (510), and DIND (511 -> 512, 513, 514) */
	for (i = 0; i < EXT4_NDIR_BLOCKS; i++)
		EXT4_I(inode)->i_data[i] = cpu_to_le32(120 + i);

	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(510);
	ind = ind_blk_ptrs(test, 510);
	for (i = 0; i < 12; i++)
		ind[i] = cpu_to_le32(620 + i);

	EXT4_I(inode)->i_data[EXT4_DIND_BLOCK] = cpu_to_le32(511);
	dind = ind_blk_ptrs(test, 511);
	dind[0] = cpu_to_le32(512);
	dind[1] = cpu_to_le32(513);
	dind[2] = cpu_to_le32(514);
	c0 = ind_blk_ptrs(test, 512);
	c1 = ind_blk_ptrs(test, 513);
	c2 = ind_blk_ptrs(test, 514);
	for (i = 0; i < 8; i++) {
		c0[i] = cpu_to_le32(720 + i);
		c1[i] = cpu_to_le32(740 + i);
		c2[i] = cpu_to_le32(760 + i);
	}

	/* 1. Direct-only hole punch [3, 8) */
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_remove_space(handle, inode, 3, 8);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[2]), 122U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[3]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[7]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[8]), 128U);

	/* 2. Same-level IND convergence [IND_BASE + 3, IND_BASE + 8) */
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_remove_space(handle, inode, IND_TEST_IND_BASE + 3,
				    IND_TEST_IND_BASE + 8);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ind[2]), 622U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ind[3]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ind[7]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ind[8]), 628U);

	/*
	 * 3. Same-level DIND convergence across 3 IND children:
	 * [DIND_BASE + 4, DIND_BASE + 2 * 256 + 3) frees c0[4..7], the entire
	 * middle c1 (513 + 740..747), and c2[0..2], preserving c0[0..3] and
	 * c2[3..7].
	 */
	ctx->freed_cnt = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_remove_space(handle, inode, IND_TEST_DIND_BASE + 4,
				    IND_TEST_DIND_BASE +
				    2 * IND_TEST_PTRS_PER_BLK + 3);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c0[3]), 723U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c0[4]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(dind[1]), 0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 513));
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 740));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c2[2]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c2[3]), 763U);

	/*
	 * 4. Cross-level punch (n = 1 < n2 = 3): [10, DIND_BASE + 2) frees
	 * direct [10..11], entire IND (510), and c0[0..1], preserving c0[2..3].
	 */
	ctx->freed_cnt = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ind_remove_space(handle, inode, 10, IND_TEST_DIND_BASE + 2);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[9]), 129U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[10]), 0U);
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_IND_BLOCK]), 0U);
	KUNIT_EXPECT_TRUE(test, ind_was_freed(ctx, 510));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c0[1]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(c0[2]), 722U);

	ext4_journal_stop(handle);
	iput(inode);
}

/*
 * Test 7: Corrupt out-of-range block pointers in ext4_clear_blocks() and
 * ext4_free_branches() record filesystem errors without freeing invalid blocks.
 */
static void test_ind_free_corrupt_block_numbers(struct kunit *test)
{
	struct ind_test_ctx *ctx = test->priv;
	struct inode *inode = ind_make_test_inode(test, 18);
	ext4_fsblk_t bad_blk = IND_TEST_TOTAL_BLKS + 77;
	handle_t *handle;

	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, 4);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));

	EXT4_I(inode)->i_data[0] = cpu_to_le32(bad_blk);
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(bad_blk + 1);
	i_size_write(inode, 0);

	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_ind_truncate(handle, inode);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_TRUE(test, ctx->sbi.s_mount_state & EXT4_ERROR_FS);
	KUNIT_EXPECT_FALSE(test, ind_was_freed(ctx, bad_blk));
	KUNIT_EXPECT_FALSE(test, ind_was_freed(ctx, bad_blk + 1));

	ext4_journal_stop(handle);
	iput(inode);
}

static struct kunit_case ext4_indirect_test_cases[] = {
	KUNIT_CASE(test_block_to_path_and_trans_blocks),
	KUNIT_CASE(test_ind_map_blocks_lookup_mapped_and_holes),
	KUNIT_CASE(test_ind_map_blocks_alloc_direct_ind_dind_tind),
	KUNIT_CASE(test_ind_map_blocks_errors_and_alloc_branch_unwind),
	KUNIT_CASE(test_ind_truncate_partial_and_full),
	KUNIT_CASE(test_ind_remove_space_levels_and_convergence),
	KUNIT_CASE(test_ind_free_corrupt_block_numbers),
	{}
};

static struct kunit_suite ext4_indirect_test_suite = {
	.name = "ext4_indirect_test",
	.init = ind_test_init,
	.test_cases = ext4_indirect_test_cases,
};

kunit_test_suites(&ext4_indirect_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 indirect block mapping and truncation");
MODULE_LICENSE("GPL");
