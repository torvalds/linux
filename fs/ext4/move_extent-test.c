// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/move_extent.c
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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_extents.h"
#include "ext4_jbd2.h"

#define MEXT_TEST_BLK_SIZE		4096U
#define MEXT_TEST_BLK_BITS		12U
#define MEXT_TEST_BLOCKS_PER_GRP	32768U
#define MEXT_TEST_NGROUPS		8U
#define MEXT_TEST_FIRST_DATA_BLK	1U
#define MEXT_TEST_TOTAL_BLKS		(MEXT_TEST_FIRST_DATA_BLK + \
					 MEXT_TEST_NGROUPS * \
					 MEXT_TEST_BLOCKS_PER_GRP)
#define MEXT_MAX_META_BLKS		16U
#define MEXT_META_BASE_PBLK		1000U

struct mext_meta_blk {
	ext4_fsblk_t pblk;
	struct buffer_head bh;
	u8 data[MEXT_TEST_BLK_SIZE];
	bool in_use;
};

struct mext_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block *es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct address_space bdev_mapping;
	struct buffer_head sbh;
	u8 sbh_data[MEXT_TEST_BLK_SIZE];
	struct mext_meta_blk *meta_blks;
	ext4_fsblk_t next_meta_pblk;
	struct inode *orig_inode;
	struct inode *donor_inode;
	struct dentry orig_dentry;
	struct dentry donor_dentry;
	struct file orig_file;
	struct file donor_file;
	bool es_shrinker_inited;
	bool counters_inited;
};

static struct kunit *active_mext_test;

static void mext_noop_work(struct work_struct *work)
{
}

static void mext_noop_timer(struct timer_list *t)
{
}

static unsigned long mext_dummy_shrinker_count(struct shrinker *shrink,
					       struct shrink_control *sc)
{
	return 0;
}

static unsigned long mext_dummy_shrinker_scan(struct shrinker *shrink,
					      struct shrink_control *sc)
{
	return SHRINK_STOP;
}

static struct inode *mext_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = sizeof(struct ext4_inode) - EXT4_GOOD_OLD_INODE_SIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void mext_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void mext_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **mext_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *mext_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations mext_dummy_qops = {
	.get_reserved_space	= mext_get_reserved_space,
};
#endif

static const struct super_operations mext_test_sops = {
	.alloc_inode	= mext_test_alloc_inode,
	.free_inode	= mext_test_free_inode,
	.evict_inode	= mext_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= mext_get_dquots,
#endif
};

static int mext_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static int mext_set_super(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static struct file_system_type mext_test_fs_type = {
	.name			= "ext4_move_extent_test",
	.init_fs_context	= mext_init_fs_context,
	.kill_sb		= kill_anon_super,
};

static int mext_ext_dirty_stub(const char *where, unsigned int line,
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

static int mext_handle_dirty_metadata_stub(const char *where,
					   unsigned int line,
					   handle_t *handle,
					   struct inode *inode,
					   struct buffer_head *bh)
{
	if (bh)
		set_buffer_uptodate(bh);
	return 0;
}

static int mext_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
				      const char *func, unsigned int line)
{
	return 0;
}

static struct buffer_head *mext_ext_getblk_stub(struct inode *inode,
						ext4_fsblk_t pblk,
						gfp_t gfp_flags)
{
	struct kunit *test = active_mext_test;
	struct mext_test_ctx *ctx;
	struct mext_meta_blk *slot = NULL;
	unsigned int i;

	if (!test)
		return NULL;
	ctx = test->priv;
	if (!ctx || !ctx->meta_blks)
		return NULL;

	for (i = 0; i < MEXT_MAX_META_BLKS; i++) {
		if (ctx->meta_blks[i].in_use &&
		    ctx->meta_blks[i].pblk == pblk) {
			slot = &ctx->meta_blks[i];
			break;
		}
	}

	if (!slot) {
		for (i = 0; i < MEXT_MAX_META_BLKS; i++) {
			if (!ctx->meta_blks[i].in_use) {
				slot = &ctx->meta_blks[i];
				break;
			}
		}
		if (!slot)
			return NULL;

		memset(slot, 0, sizeof(*slot));
		slot->in_use = true;
		slot->pblk = pblk;
		slot->bh.b_data = slot->data;
		slot->bh.b_size = inode->i_sb->s_blocksize;
		slot->bh.b_blocknr = pblk;
		slot->bh.b_bdev = inode->i_sb->s_bdev;
		atomic_set(&slot->bh.b_count, 1);
		set_buffer_uptodate(&slot->bh);
		set_buffer_mapped(&slot->bh);
	}

	get_bh(&slot->bh);
	return &slot->bh;
}

static ext4_fsblk_t mext_mb_new_blocks_stub(handle_t *handle,
					    struct ext4_allocation_request *ar,
					    int *errp)
{
	struct kunit *test = active_mext_test;
	struct mext_test_ctx *ctx;
	ext4_fsblk_t pblk;

	if (!test) {
		*errp = -EIO;
		return 0;
	}
	ctx = test->priv;
	pblk = ctx->next_meta_pblk;
	ctx->next_meta_pblk += ar->len;
	*errp = 0;
	return pblk;
}

static void mext_free_blocks_stub(handle_t *handle, struct inode *inode,
				  struct buffer_head *bh, ext4_fsblk_t block,
				  unsigned long count, int flags)
{
}

static int mext_journal_data_mode_stub(struct inode *inode)
{
	return EXT4_INODE_JOURNAL_DATA_MODE;
}

static void mext_init_mock_file(struct file *filp, struct dentry *dentry,
				struct inode *inode)
{
	struct file tmp = {
		.f_path		= { .dentry = dentry },
		.f_inode	= inode,
		.f_mapping	= inode->i_mapping,
		.f_mode		= FMODE_READ | FMODE_WRITE | FMODE_NOCMTIME,
	};

	memset(dentry, 0, sizeof(*dentry));
	dentry->d_inode = inode;
	memcpy(filp, &tmp, sizeof(*filp));
}

static struct inode *mext_create_test_inode(struct kunit *test,
					    struct super_block *sb,
					    unsigned long ino,
					    ext4_lblk_t size_blks)
{
	struct ext4_extent_header *eh;
	struct inode *inode;

	inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);

	inode->i_ino = ino;
	inode->i_mode = S_IFREG | 0644;
	set_nlink(inode, 1);
	inode->i_blkbits = sb->s_blocksize_bits;
	/*
	 * Note: leave S_NOQUOTA clear so ext4_is_quota_file(inode) is false.
	 */
	inode->i_flags |= S_NOSEC;
	inode_state_set_raw(inode,
			    I_DIRTY_SYNC | I_DIRTY_DATASYNC | I_DIRTY_PAGES);
	mapping_set_gfp_mask(&inode->i_data, GFP_KERNEL);

	EXT4_I(inode)->i_csum_seed = 0x13572468U;
	EXT4_I(inode)->i_disksize = (loff_t)size_blks << sb->s_blocksize_bits;
	i_size_write(inode, EXT4_I(inode)->i_disksize);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_aops(inode);

	eh = ext_inode_hdr(inode);
	memset(EXT4_I(inode)->i_data, 0, sizeof(EXT4_I(inode)->i_data));
	eh->eh_magic = EXT4_EXT_MAGIC;
	eh->eh_entries = 0;
	eh->eh_max = cpu_to_le16(ext4_ext_space_root_idx_test(inode, 0));
	eh->eh_depth = 0;

	return inode;
}

static void mext_insert_extent(struct kunit *test, struct inode *inode,
			       ext4_lblk_t lblk, u16 len, ext4_fsblk_t pblk,
			       bool unwritten)
{
	struct ext4_extent newex = {};
	struct ext4_ext_path *path;

	newex.ee_block = cpu_to_le32(lblk);
	newex.ee_len = cpu_to_le16(len);
	if (unwritten)
		ext4_ext_mark_unwritten(&newex);
	ext4_ext_store_pblock(&newex, pblk);

	inode_lock(inode);
	down_write(&EXT4_I(inode)->i_data_sem);
	path = ext4_find_extent(inode, lblk, NULL, 0);
	if (!IS_ERR(path))
		path = ext4_ext_insert_extent(NULL, inode, path, &newex, 0);
	up_write(&EXT4_I(inode)->i_data_sem);
	inode_unlock(inode);

	KUNIT_ASSERT_FALSE(test, IS_ERR(path));
	ext4_free_ext_path(path);
}

static void mext_populate_folio_block(struct kunit *test, struct inode *inode,
				      ext4_lblk_t lblk, u8 pattern)
{
	loff_t pos = (loff_t)lblk << inode->i_blkbits;
	size_t bsize = i_blocksize(inode);
	struct folio *folio;
	u8 *addr;

	folio = __filemap_get_folio(inode->i_mapping, pos >> PAGE_SHIFT,
				    FGP_CREAT | FGP_LOCK, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));

	addr = folio_address(folio);
	memset(addr + offset_in_folio(folio, pos), pattern, bsize);
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	folio_put(folio);
}

static void mext_assert_mapped_block(struct kunit *test, struct inode *inode,
				     ext4_lblk_t lblk, ext4_fsblk_t exp_pblk,
				     unsigned int exp_flag)
{
	struct ext4_map_blocks map = {
		.m_lblk	= lblk,
		.m_len	= 1,
	};
	int ret;

	inode_lock(inode);
	ret = ext4_map_blocks(NULL, inode, &map, 0);
	inode_unlock(inode);

	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, map.m_pblk, exp_pblk);
	KUNIT_EXPECT_TRUE(test, (map.m_flags & exp_flag) != 0);
	if (exp_flag == EXT4_MAP_MAPPED)
		KUNIT_EXPECT_EQ(test, map.m_flags & EXT4_MAP_UNWRITTEN, 0U);
	else if (exp_flag == EXT4_MAP_UNWRITTEN)
		KUNIT_EXPECT_EQ(test, map.m_flags & EXT4_MAP_MAPPED, 0U);
}

static void mext_cleanup_ctx(void *data)
{
	struct mext_test_ctx *ctx = data;

	active_mext_test = NULL;

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);

	if (ctx->orig_inode) {
		iput(ctx->orig_inode);
		ctx->orig_inode = NULL;
	}
	if (ctx->donor_inode) {
		iput(ctx->donor_inode);
		ctx->donor_inode = NULL;
	}

	if (ctx->sb) {
		deactivate_super(ctx->sb);
		ctx->sb = NULL;
	}
	rcu_barrier();

	if (ctx->es_shrinker_inited) {
		ext4_es_unregister_shrinker(&ctx->sbi);
		ext4_es_destroy_stats(&ctx->sbi);
		ctx->es_shrinker_inited = false;
	}
	if (ctx->counters_inited) {
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
		ctx->counters_inited = false;
	}

	if (ctx->bdev.bd_stats) {
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
	}
}

static struct mext_test_ctx *mext_setup_ctx(struct kunit *test)
{
	struct mext_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	int err;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->es = kunit_kzalloc(test, sizeof(*ctx->es), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->es);
	ctx->meta_blks = kunit_kcalloc(test, MEXT_MAX_META_BLKS,
				       sizeof(*ctx->meta_blks), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->meta_blks);
	ctx->next_meta_pblk = MEXT_META_BASE_PBLK;

	fc = fs_context_for_mount(&mext_test_fs_type, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(fc));
	sb = sget_fc(fc, NULL, mext_set_super);
	put_fs_context(fc);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb));
	up_write(&sb->s_umount);
	ctx->sb = sb;

	INIT_WORK(&ctx->sbi.s_sb_upd_work, mext_noop_work);
	timer_setup(&ctx->sbi.s_err_report, mext_noop_timer, 0);

	err = kunit_add_action_or_reset(test, mext_cleanup_ctx, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	KUNIT_ASSERT_TRUE(test, ctx->bdev.bd_stats != NULL);
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bdev_mapping;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);

	sb->s_bdev = &ctx->bdev;
	sb->s_blocksize = MEXT_TEST_BLK_SIZE;
	sb->s_blocksize_bits = MEXT_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic = EXT4_SUPER_MAGIC;
	sb->s_op = &mext_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &mext_dummy_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_fs_info = &ctx->sbi;
	strscpy(sb->s_id, "ext4_mext_kunit", sizeof(sb->s_id));

	ctx->sbh.b_data = ctx->sbh_data;
	ctx->sbh.b_size = MEXT_TEST_BLK_SIZE;
	ctx->sbh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sbh.b_count, 2);
	set_buffer_uptodate(&ctx->sbh);
	clear_buffer_mapped(&ctx->sbh);

	ctx->es->s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es->s_first_data_block = cpu_to_le32(MEXT_TEST_FIRST_DATA_BLK);
	ctx->es->s_log_block_size = cpu_to_le32(2);
	ctx->es->s_log_cluster_size = cpu_to_le32(2);
	ctx->es->s_blocks_per_group = cpu_to_le32(MEXT_TEST_BLOCKS_PER_GRP);
	ctx->es->s_clusters_per_group = cpu_to_le32(MEXT_TEST_BLOCKS_PER_GRP);
	ctx->es->s_inodes_per_group = cpu_to_le32(8192);
	ctx->es->s_inodes_count = cpu_to_le32(MEXT_TEST_NGROUPS * 8192);
	ctx->es->s_feature_incompat = cpu_to_le32(EXT4_FEATURE_INCOMPAT_EXTENTS);
	ext4_blocks_count_set(ctx->es, MEXT_TEST_TOTAL_BLKS);

	bgl_lock_init(&ctx->bgl);
	ctx->sbi.s_sb = sb;
	ctx->sbi.s_es = ctx->es;
	ctx->sbi.s_sbh = &ctx->sbh;
	ctx->sbi.s_blockgroup_lock = &ctx->bgl;
	ctx->sbi.s_blocks_per_group = MEXT_TEST_BLOCKS_PER_GRP;
	ctx->sbi.s_clusters_per_group = MEXT_TEST_BLOCKS_PER_GRP;
	ctx->sbi.s_cluster_ratio = 1;
	ctx->sbi.s_cluster_bits = 0;
	ctx->sbi.s_groups_count = MEXT_TEST_NGROUPS;
	ctx->sbi.s_desc_size = EXT4_MIN_DESC_SIZE;
	ctx->sbi.s_desc_per_block = MEXT_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE;
	ctx->sbi.s_inodes_per_group = 8192;
	ctx->sbi.s_inode_size = sizeof(struct ext4_inode);
	ctx->sbi.s_inodes_per_block = MEXT_TEST_BLK_SIZE /
				      sizeof(struct ext4_inode);
	ctx->sbi.s_bitmap_maxbytes = MAX_LFS_FILESIZE;
	set_opt(sb, ERRORS_CONT);

	mutex_init(&ctx->sbi.s_orphan_lock);
	INIT_LIST_HEAD(&ctx->sbi.s_orphan);
	spin_lock_init(&ctx->sbi.s_bdev_wb_lock);
	spin_lock_init(&ctx->sbi.s_error_lock);
	spin_lock_init(&ctx->sbi.s_md_lock);
	INIT_LIST_HEAD(&ctx->sbi.s_fc_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&ctx->sbi.s_fc_q[FC_Q_STAGING]);
	INIT_LIST_HEAD(&ctx->sbi.s_fc_dentry_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&ctx->sbi.s_fc_dentry_q[FC_Q_STAGING]);
	mutex_init(&ctx->sbi.s_fc_lock);
	ratelimit_state_init(&ctx->sbi.s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);
	ratelimit_state_init(&ctx->sbi.s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);
	ratelimit_state_init(&ctx->sbi.s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);

	err = percpu_counter_init(&ctx->sbi.s_freeclusters_counter, 65536,
				  GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);
	err = percpu_counter_init(&ctx->sbi.s_dirtyclusters_counter, 0,
				  GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		KUNIT_FAIL_AND_ABORT(test, "dirtyclusters counter init failed");
	}
	ctx->counters_inited = true;

	err = ext4_es_register_shrinker(&ctx->sbi);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->sbi.s_es_shrinker->count_objects = mext_dummy_shrinker_count;
	ctx->sbi.s_es_shrinker->scan_objects = mext_dummy_shrinker_scan;
	ctx->es_shrinker_inited = true;

	ctx->orig_inode = mext_create_test_inode(test, sb, 21, 16);
	ctx->donor_inode = mext_create_test_inode(test, sb, 22, 16);
	mext_init_mock_file(&ctx->orig_file, &ctx->orig_dentry,
			    ctx->orig_inode);
	mext_init_mock_file(&ctx->donor_file, &ctx->donor_dentry,
			    ctx->donor_inode);

	test->priv = ctx;
	active_mext_test = test;

	kunit_activate_static_stub(test, __ext4_ext_dirty,
				   mext_ext_dirty_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   mext_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   mext_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, ext4_ext_getblk,
				   mext_ext_getblk_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   mext_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   mext_free_blocks_stub);

	return ctx;
}

static void test_double_data_sem_lock_order(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *low = ctx->orig_inode;
	struct inode *high = ctx->donor_inode;

	if (low > high)
		swap(low, high);

	/* Acquire with first < second */
	ext4_double_down_write_data_sem(low, high);
	KUNIT_EXPECT_TRUE(test, rwsem_is_locked(&EXT4_I(low)->i_data_sem));
	KUNIT_EXPECT_TRUE(test, rwsem_is_locked(&EXT4_I(high)->i_data_sem));
	ext4_double_up_write_data_sem(low, high);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&EXT4_I(low)->i_data_sem));
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&EXT4_I(high)->i_data_sem));

	/* Acquire with first > second */
	ext4_double_down_write_data_sem(high, low);
	KUNIT_EXPECT_TRUE(test, rwsem_is_locked(&EXT4_I(low)->i_data_sem));
	KUNIT_EXPECT_TRUE(test, rwsem_is_locked(&EXT4_I(high)->i_data_sem));
	ext4_double_up_write_data_sem(high, low);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&EXT4_I(low)->i_data_sem));
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&EXT4_I(high)->i_data_sem));
}

static void test_mext_check_validity_rejections(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *orig = ctx->orig_inode;
	struct inode *donor = ctx->donor_inode;
	struct super_block *other_sb;
	__u64 moved_len = 99;
	int ret;

	/* Valid baseline */
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor), 0);

	/* Same inode rejected both directly and via ext4_move_extents() */
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, orig),
			-EINVAL);
	ret = ext4_move_extents(&ctx->orig_file, &ctx->orig_file, 0, 0, 1,
				&moved_len);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, moved_len, 0ULL);

	/* Cross-superblock rejected */
	other_sb = kunit_kzalloc(test, sizeof(*other_sb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other_sb);
	donor->i_sb = other_sb;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	donor->i_sb = ctx->sb;

	/* Non-regular files rejected */
	orig->i_mode = S_IFIFO | 0644;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	ret = ext4_move_extents(&ctx->orig_file, &ctx->donor_file, 0, 0, 1,
				&moved_len);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	orig->i_mode = S_IFREG | 0644;

	donor->i_mode = S_IFDIR | 0755;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	donor->i_mode = S_IFREG | 0644;

	/* Bigalloc feature rejected */
	ext4_set_feature_bigalloc(ctx->sb);
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	ext4_clear_feature_bigalloc(ctx->sb);

#if IS_ENABLED(CONFIG_FS_DAX)
	/* DAX inode rejected */
	orig->i_flags |= S_DAX;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	orig->i_flags &= ~S_DAX;
#endif

	/* Data journaling mode rejected */
	kunit_activate_static_stub(test, ext4_inode_journal_mode,
				   mext_journal_data_mode_stub);
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	kunit_deactivate_static_stub(test, ext4_inode_journal_mode);

	/* Encrypted inode rejected */
	donor->i_flags |= S_ENCRYPTED;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	donor->i_flags &= ~S_ENCRYPTED;

	/* Non-extent inode rejected */
	ext4_clear_inode_flag(orig, EXT4_INODE_EXTENTS);
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	ext4_set_inode_flag(orig, EXT4_INODE_EXTENTS);

	/* SUID / SGID on donor rejected */
	donor->i_mode |= S_ISUID;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	donor->i_mode = (donor->i_mode & ~S_ISUID) | S_ISGID;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	donor->i_mode &= ~S_ISGID;

	/* Immutable / append-only donor rejected with -EPERM */
	donor->i_flags |= S_IMMUTABLE;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EPERM);
	donor->i_flags = (donor->i_flags & ~S_IMMUTABLE) | S_APPEND;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EPERM);
	donor->i_flags &= ~S_APPEND;

	/* Swapfile rejected with -ETXTBSY */
	orig->i_flags |= S_SWAPFILE;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-ETXTBSY);
	orig->i_flags &= ~S_SWAPFILE;

	/* Quota file (S_NOQUOTA without EXT4_INODE_EA_INODE) rejected */
	orig->i_flags |= S_NOQUOTA;
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EOPNOTSUPP);
	ext4_set_inode_flag(orig, EXT4_INODE_EA_INODE);
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor), 0);
	ext4_clear_inode_flag(orig, EXT4_INODE_EA_INODE);
	orig->i_flags &= ~S_NOQUOTA;

	/* Zero-byte file rejected with -EINVAL */
	i_size_write(donor, 0);
	KUNIT_EXPECT_EQ(test, ext4_mext_check_validity_test(orig, donor),
			-EINVAL);
	i_size_write(donor, EXT4_I(donor)->i_disksize);
}

static void test_mext_check_adjust_range(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *orig = ctx->orig_inode;
	struct inode *donor = ctx->donor_inode;
	__u64 len;
	int ret;

	/* Sub-page block alignment check when blocksize (1024) < PAGE_SIZE */
	orig->i_blkbits = 10;
	donor->i_blkbits = 10;
	i_size_write(orig, max_t(loff_t, 32 * 1024, 4 * PAGE_SIZE));
	i_size_write(donor, max_t(loff_t, 32 * 1024, 4 * PAGE_SIZE));

	len = 4;
	ret = ext4_mext_check_adjust_range_test(orig, donor, 1, 2, &len);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	len = 4;
	ret = ext4_mext_check_adjust_range_test(orig, donor, 1,
						1 + (PAGE_SIZE >> 10), &len);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, len, 4ULL);

	orig->i_blkbits = MEXT_TEST_BLK_BITS;
	donor->i_blkbits = MEXT_TEST_BLK_BITS;

	/* EXT_MAX_BLOCKS boundary and sum-overflow checks */
	len = 1;
	KUNIT_EXPECT_EQ(test,
			ext4_mext_check_adjust_range_test(orig, donor,
							  EXT_MAX_BLOCKS,
							  EXT_MAX_BLOCKS, &len),
			-EINVAL);

	len = (u64)EXT_MAX_BLOCKS + 1;
	KUNIT_EXPECT_EQ(test,
			ext4_mext_check_adjust_range_test(orig, donor, 0, 0,
							  &len),
			-EINVAL);

	len = 4;
	KUNIT_EXPECT_EQ(test,
			ext4_mext_check_adjust_range_test(orig, donor,
							  EXT_MAX_BLOCKS - 4,
							  EXT_MAX_BLOCKS - 4,
							  &len),
			-EINVAL);

	/* Clamping to shorter EOF (including partial trailing block i_size) */
	i_size_write(orig, 5 * MEXT_TEST_BLK_SIZE + 100);  /* orig_eof = 6 */
	i_size_write(donor, 4 * MEXT_TEST_BLK_SIZE);       /* donor_eof = 4 */

	len = 12;
	ret = ext4_mext_check_adjust_range_test(orig, donor, 1, 1, &len);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, len, 3ULL);

	/* Start at or beyond EOF clamps len to 0 and returns -EINVAL */
	len = 4;
	ret = ext4_mext_check_adjust_range_test(orig, donor, 4, 4, &len);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, len, 0ULL);
}

static void test_move_extents_holes_and_unwritten(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *orig = ctx->orig_inode;
	struct inode *donor = ctx->donor_inode;
	__u64 moved_len = 0;
	int ret;

	/*
	 * orig:  [0, 2) hole, [2, 4) unwritten -> pblk 202..203,
	 *        [4, 6) unwritten -> pblk 204..205
	 * donor: [0, 2) unwritten -> pblk 300..301, [2, 4) hole,
	 *        [4, 6) unwritten -> pblk 304..305
	 *
	 * Expected across [0, 6) (clamped from requested len = 16 to EOF = 6):
	 *   - [0, 2): skipped in ext4_move_extents() because orig is a hole
	 *   - [2, 4): skipped via MEXT_SKIP_EXTENT because donor is a hole
	 *   - [4, 6): swapped via MEXT_MOVE_EXTENT (both unwritten)
	 */
	i_size_write(orig, 6 * MEXT_TEST_BLK_SIZE);
	EXT4_I(orig)->i_disksize = 6 * MEXT_TEST_BLK_SIZE;
	i_size_write(donor, 6 * MEXT_TEST_BLK_SIZE);
	EXT4_I(donor)->i_disksize = 6 * MEXT_TEST_BLK_SIZE;

	mext_insert_extent(test, orig, 2, 4, 202, true);
	mext_insert_extent(test, donor, 0, 2, 300, true);
	mext_insert_extent(test, donor, 4, 2, 304, true);

	ret = ext4_move_extents(&ctx->orig_file, &ctx->donor_file, 0, 0, 16,
				&moved_len);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, moved_len, 2ULL);

	/* [2, 4) on orig remained on pblk 202..203; [4, 6) swapped to 304..305 */
	mext_assert_mapped_block(test, orig, 2, 202, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, orig, 3, 203, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, orig, 4, 304, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, orig, 5, 305, EXT4_MAP_UNWRITTEN);

	/* [0, 2) on donor remained 300..301; [4, 6) received 204..205 */
	mext_assert_mapped_block(test, donor, 0, 300, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, 4, 204, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, 5, 205, EXT4_MAP_UNWRITTEN);
}

static void test_move_extents_written_copy_data_and_split(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *orig = ctx->orig_inode;
	struct inode *donor = ctx->donor_inode;
	ext4_lblk_t stride = max_t(ext4_lblk_t, 1,
				   PAGE_SIZE >> MEXT_TEST_BLK_BITS);
	ext4_lblk_t ext1 = stride;
	ext4_lblk_t ext2 = 3 * stride;
	ext4_lblk_t total_blks = ext2 + 3;
	ext4_lblk_t moved_lblks[] = { ext1, ext2, ext2 + 1 };
	ext4_lblk_t lblk;
	__u64 moved_len = 0;
	unsigned int i;
	int ret;

	/*
	 * orig:  [0, total_blks) written -> pblk 400.., uptodate folios
	 * donor: [ext1, ext1 + 1) written -> pblk 501,
	 *        [ext1 + 1, ext2) hole,
	 *        [ext2, ext2 + 2) unwritten -> pblk 503..504
	 *
	 * Spacing donor extents by @stride ensures distinct pagecache folios on
	 * 16K/64K PAGE_SIZE kernels so dirtying ext1's folio does not make
	 * ext2's folio unreleasable (-EBUSY).
	 *
	 * Moving [ext1, ext2 + 2) exercises MEXT_COPY_DATA, left/right extent
	 * splits on orig, donor hole skipping, written preservation on orig,
	 * mark_unwritten=1 on donor, and page cache data preservation.
	 */
	i_size_write(orig, (loff_t)total_blks * MEXT_TEST_BLK_SIZE);
	EXT4_I(orig)->i_disksize = (loff_t)total_blks * MEXT_TEST_BLK_SIZE;
	i_size_write(donor, (loff_t)total_blks * MEXT_TEST_BLK_SIZE);
	EXT4_I(donor)->i_disksize = (loff_t)total_blks * MEXT_TEST_BLK_SIZE;

	mext_insert_extent(test, orig, 0, total_blks, 400, false);
	mext_insert_extent(test, donor, ext1, 1, 501, false);
	mext_insert_extent(test, donor, ext2, 2, 503, true);

	for (lblk = 0; lblk < total_blks; lblk++)
		mext_populate_folio_block(test, orig, lblk, 0xA0 + lblk);

	ret = ext4_move_extents(&ctx->orig_file, &ctx->donor_file, ext1, ext1,
				ext2 + 2 - ext1, &moved_len);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, moved_len, 3ULL);

	/* Verify orig mappings: leading/hole/trailing unchanged; moved swapped */
	mext_assert_mapped_block(test, orig, 0, 400, EXT4_MAP_MAPPED);
	mext_assert_mapped_block(test, orig, ext1, 501, EXT4_MAP_MAPPED);
	mext_assert_mapped_block(test, orig, ext1 + 1, 400 + ext1 + 1,
				 EXT4_MAP_MAPPED);
	mext_assert_mapped_block(test, orig, ext2, 503, EXT4_MAP_MAPPED);
	mext_assert_mapped_block(test, orig, ext2 + 1, 504, EXT4_MAP_MAPPED);
	mext_assert_mapped_block(test, orig, ext2 + 2, 400 + ext2 + 2,
				 EXT4_MAP_MAPPED);

	/* Donor's swapped extents are marked unwritten to avoid leaking data */
	mext_assert_mapped_block(test, donor, ext1, 400 + ext1,
				 EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, ext2, 400 + ext2,
				 EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, ext2 + 1, 400 + ext2 + 1,
				 EXT4_MAP_UNWRITTEN);

	/* Verify cached bytes in orig's moved folios were preserved & dirtied */
	for (i = 0; i < ARRAY_SIZE(moved_lblks); i++) {
		loff_t pos;
		struct folio *folio;
		u8 *addr;

		lblk = moved_lblks[i];
		pos = (loff_t)lblk << orig->i_blkbits;
		folio = filemap_get_folio(orig->i_mapping, pos >> PAGE_SHIFT);
		KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
		KUNIT_EXPECT_TRUE(test, folio_test_uptodate(folio));
		KUNIT_EXPECT_TRUE(test, folio_test_dirty(folio));
		addr = folio_address(folio);
		KUNIT_EXPECT_EQ(test, addr[offset_in_folio(folio, pos)],
				(u8)(0xA0 + lblk));
		folio_put(folio);
	}
}

static void test_move_extents_unwritten_orig_and_ebusy(struct kunit *test)
{
	struct mext_test_ctx *ctx = mext_setup_ctx(test);
	struct inode *orig = ctx->orig_inode;
	struct inode *donor = ctx->donor_inode;
	ext4_lblk_t stride = max_t(ext4_lblk_t, 1,
				   PAGE_SIZE >> MEXT_TEST_BLK_BITS);
	ext4_lblk_t ebusy_lblk = max_t(ext4_lblk_t, 2, stride);
	ext4_lblk_t total_blks = ebusy_lblk + 2;
	struct folio *donor_folio;
	__u64 moved_len = 0;
	int ret;

	/*
	 * Part 1: orig unwritten [0, 2) -> 600..601 (no pre-populated folios),
	 * donor written [0, 2) -> 700..701. Exercises MEXT_COPY_DATA through
	 * mext_folio_mkuptodate()'s non-uptodate unwritten buffer zeroing path.
	 */
	i_size_write(orig, (loff_t)total_blks * MEXT_TEST_BLK_SIZE);
	EXT4_I(orig)->i_disksize = (loff_t)total_blks * MEXT_TEST_BLK_SIZE;
	i_size_write(donor, (loff_t)total_blks * MEXT_TEST_BLK_SIZE);
	EXT4_I(donor)->i_disksize = (loff_t)total_blks * MEXT_TEST_BLK_SIZE;

	mext_insert_extent(test, orig, 0, 2, 600, true);
	mext_insert_extent(test, donor, 0, 2, 700, false);

	ret = ext4_move_extents(&ctx->orig_file, &ctx->donor_file, 0, 0, 2,
				&moved_len);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, moved_len, 2ULL);

	mext_assert_mapped_block(test, orig, 0, 700, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, orig, 1, 701, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, 0, 600, EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, 1, 601, EXT4_MAP_UNWRITTEN);

	/*
	 * Part 2: non-releasable folio on donor triggers -EBUSY from
	 * filemap_release_folio() without altering extent mappings.
	 */
	mext_insert_extent(test, orig, ebusy_lblk, 1, 602, true);
	mext_insert_extent(test, donor, ebusy_lblk, 1, 702, true);

	donor_folio = __filemap_get_folio(donor->i_mapping,
					  ((loff_t)ebusy_lblk <<
					   donor->i_blkbits) >> PAGE_SHIFT,
					  FGP_CREAT | FGP_LOCK, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(donor_folio));
	if (!folio_buffers(donor_folio))
		create_empty_buffers(donor_folio, MEXT_TEST_BLK_SIZE, 0);
	folio_set_checked(donor_folio);
	folio_unlock(donor_folio);

	ret = ext4_move_extents(&ctx->orig_file, &ctx->donor_file,
				ebusy_lblk, ebusy_lblk, 1, &moved_len);
	folio_clear_checked(donor_folio);
	folio_put(donor_folio);

	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	KUNIT_EXPECT_EQ(test, moved_len, 0ULL);
	mext_assert_mapped_block(test, orig, ebusy_lblk, 602,
				 EXT4_MAP_UNWRITTEN);
	mext_assert_mapped_block(test, donor, ebusy_lblk, 702,
				 EXT4_MAP_UNWRITTEN);
}

static struct kunit_case move_extent_test_cases[] = {
	KUNIT_CASE(test_double_data_sem_lock_order),
	KUNIT_CASE(test_mext_check_validity_rejections),
	KUNIT_CASE(test_mext_check_adjust_range),
	KUNIT_CASE(test_move_extents_holes_and_unwritten),
	KUNIT_CASE(test_move_extents_written_copy_data_and_split),
	KUNIT_CASE(test_move_extents_unwritten_orig_and_ebusy),
	{}
};

static struct kunit_suite move_extent_test_suite = {
	.name = "ext4_move_extent_test",
	.test_cases = move_extent_test_cases,
};

kunit_test_suites(&move_extent_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 move_extent");
MODULE_LICENSE("GPL");
