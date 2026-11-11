// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/migrate.c
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
#include "ext4_extents.h"
#include "ext4_jbd2.h"

#define MIGRATE_TEST_BLK_SIZE		1024U
#define MIGRATE_TEST_BLK_BITS		10U
#define MIGRATE_TEST_BLOCKS_PER_GRP	1024U
#define MIGRATE_TEST_NGROUPS		8U
#define MIGRATE_TEST_FIRST_DATA_BLK	1U
#define MIGRATE_TEST_TOTAL_BLKS		(MIGRATE_TEST_FIRST_DATA_BLK + \
					 MIGRATE_TEST_NGROUPS * \
					 MIGRATE_TEST_BLOCKS_PER_GRP)
#define MIGRATE_TEST_INODE_SIZE		256U
#define MIGRATE_TEST_EXTRA_ISIZE	32U
#define MIGRATE_TEST_INODES_PER_GRP	32U
#define MIGRATE_MAX_FREED_REC		32U

struct migrate_mock_blk {
	struct list_head node;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	u8 *data;
};

struct migrate_test_ctx {
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
	int new_inode_err;
	ext4_fsblk_t freed_blks[MIGRATE_MAX_FREED_REC];
	unsigned int freed_cnt;
	bool rwsem_inited;
	bool es_shrinker_inited;
};

static struct kunit *active_migrate_test;

static struct inode *migrate_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = MIGRATE_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void migrate_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void migrate_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **migrate_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations migrate_test_sops = {
	.alloc_inode	= migrate_test_alloc_inode,
	.free_inode	= migrate_test_free_inode,
	.evict_inode	= migrate_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= migrate_test_get_dquots,
#endif
};

static int migrate_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static void migrate_test_sb_upd_work(struct work_struct *work)
{
}

static void migrate_test_err_timer(struct timer_list *t)
{
}

static struct file_system_type migrate_test_fs_type = {
	.name		 = "ext4_migrate_kunit",
	.init_fs_context = migrate_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void migrate_init_mock_bh(struct buffer_head *bh, struct folio *folio,
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

static struct buffer_head *migrate_lookup_bh(struct migrate_test_ctx *ctx,
					     ext4_fsblk_t blocknr)
{
	struct migrate_mock_blk *blk;

	list_for_each_entry(blk, &ctx->blks, node) {
		if (blk->blocknr == blocknr)
			return &blk->bh;
	}
	return NULL;
}

static struct buffer_head *migrate_get_or_create_bh(struct migrate_test_ctx *ctx,
						    ext4_fsblk_t blocknr)
{
	struct buffer_head *bh;
	struct migrate_mock_blk *blk;

	bh = migrate_lookup_bh(ctx, blocknr);
	if (bh) {
		get_bh(bh);
		return bh;
	}

	blk = kzalloc_obj(*blk);
	if (!blk)
		return NULL;
	blk->data = kzalloc(MIGRATE_TEST_BLK_SIZE, GFP_KERNEL);
	if (!blk->data) {
		kfree(blk);
		return NULL;
	}
	blk->blocknr = blocknr;
	migrate_init_mock_bh(&blk->bh, ctx->bh_folio, blk->data,
			     MIGRATE_TEST_BLK_SIZE, blocknr);
	list_add_tail(&blk->node, &ctx->blks);
	get_bh(&blk->bh);
	return &blk->bh;
}

static struct buffer_head *migrate_sb_bread_stub(struct super_block *sb,
						 sector_t block,
						 blk_opf_t op_flags)
{
	struct kunit *test = active_migrate_test;
	struct migrate_test_ctx *ctx;
	struct buffer_head *bh;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return ERR_PTR(-EIO);
	if ((ext4_fsblk_t)block >= ctx->device_blocks)
		return ERR_PTR(-EIO);

	bh = migrate_get_or_create_bh(ctx, block);
	if (!bh)
		return ERR_PTR(-ENOMEM);
	return bh;
}

static struct buffer_head *migrate_ext_getblk_stub(struct inode *inode,
						   ext4_fsblk_t pblk,
						   gfp_t gfp_flags)
{
	struct kunit *test = active_migrate_test;
	struct migrate_test_ctx *ctx;

	if (!test)
		return NULL;
	ctx = test->priv;
	if (!ctx)
		return NULL;

	return migrate_get_or_create_bh(ctx, pblk);
}

static int migrate_ext_dirty_stub(const char *where, unsigned int line,
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

static int migrate_handle_dirty_metadata_stub(const char *where,
					      unsigned int line,
					      handle_t *handle,
					      struct inode *inode,
					      struct buffer_head *bh)
{
	if (bh)
		set_buffer_uptodate(bh);
	return 0;
}

static int migrate_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
					 const char *func, unsigned int line)
{
	return 0;
}

static struct inode *migrate_new_inode_stub(const struct mnt_idmap *idmap,
					    handle_t *handle,
					    struct inode *dir, umode_t mode,
					    const struct qstr *qstr,
					    __u32 goal, uid_t *owner,
					    __u32 i_flags, int handle_type,
					    unsigned int line_no, int nblocks)
{
	struct kunit *test = active_migrate_test;
	struct migrate_test_ctx *ctx;
	struct inode *inode;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx)
		return ERR_PTR(-EIO);
	if (ctx->new_inode_err)
		return ERR_PTR(ctx->new_inode_err);

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = 99;
	inode->i_mode = mode;
	inode->i_blkbits = dir->i_sb->s_blocksize_bits;
	inode->i_flags |= S_NOQUOTA;
	EXT4_I(inode)->i_flags = i_flags;
	EXT4_I(inode)->i_csum_seed = 0xabcdef01U;
	if (owner) {
		i_uid_write(inode, owner[0]);
		i_gid_write(inode, owner[1]);
	}
	inode_state_set_raw(inode, I_NEW);
	return inode;
}

static ext4_fsblk_t migrate_mb_new_blocks_stub(handle_t *handle,
					       struct ext4_allocation_request *ar,
					       int *errp)
{
	struct kunit *test = active_migrate_test;
	struct migrate_test_ctx *ctx;
	ext4_fsblk_t pblk;

	if (!test) {
		*errp = -EIO;
		return 0;
	}
	ctx = test->priv;
	pblk = ctx->next_alloc_pblk;
	ctx->next_alloc_pblk += ar->len;
	ar->inode->i_blocks += (blkcnt_t)ar->len <<
			       (ar->inode->i_blkbits - 9);
	*errp = 0;
	return pblk;
}

static void migrate_free_blocks_stub(handle_t *handle, struct inode *inode,
				     struct buffer_head *bh, ext4_fsblk_t block,
				     unsigned long count, int flags)
{
	struct kunit *test = active_migrate_test;
	struct migrate_test_ctx *ctx;
	blkcnt_t sectors;
	unsigned long i;

	if (!test)
		return;
	ctx = test->priv;
	for (i = 0; i < count && ctx->freed_cnt < MIGRATE_MAX_FREED_REC; i++)
		ctx->freed_blks[ctx->freed_cnt++] = block + i;

	sectors = (blkcnt_t)count << (inode->i_blkbits - 9);
	if (inode->i_blocks >= sectors)
		inode->i_blocks -= sectors;
	if (bh)
		brelse(bh);
}

static bool migrate_was_freed(struct migrate_test_ctx *ctx, ext4_fsblk_t blk)
{
	unsigned int i;

	for (i = 0; i < ctx->freed_cnt; i++) {
		if (ctx->freed_blks[i] == blk)
			return true;
	}
	return false;
}

KUNIT_DEFINE_ACTION_WRAPPER(migrate_iput_action, iput, struct inode *);

static struct inode *migrate_make_test_inode(struct kunit *test, unsigned long ino,
					     umode_t mode)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct inode *inode;
	int ret;

	inode = new_inode(ctx->sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = MIGRATE_TEST_BLK_BITS;
	inode->i_flags |= S_NOQUOTA;
	EXT4_I(inode)->i_csum_seed = 0x55aa55aaU;
	ret = kunit_add_action_or_reset(test, migrate_iput_action, inode);
	KUNIT_ASSERT_EQ(test, ret, 0);
	return inode;
}

static void migrate_test_cleanup(struct migrate_test_ctx *ctx)
{
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct migrate_mock_blk *blk, *tmp;

	active_migrate_test = NULL;

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

KUNIT_DEFINE_ACTION_WRAPPER(migrate_test_cleanup_action, migrate_test_cleanup,
			    struct migrate_test_ctx *);

static int migrate_test_set_super(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static int migrate_test_init(struct kunit *test)
{
	struct migrate_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct buffer_head *sbh;
	struct inode *root_inode;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	INIT_LIST_HEAD(&ctx->blks);
	ctx->device_blocks = MIGRATE_TEST_TOTAL_BLKS;
	ctx->next_alloc_pblk = 900;
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	KUNIT_ASSERT_TRUE(test, ctx->bdev.bd_stats != NULL);

	ctx->bh_folio = folio_alloc(GFP_KERNEL, 0);
	if (!ctx->bh_folio) {
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "folio_alloc failed");
	}

	fc = fs_context_for_mount(&migrate_test_fs_type, 0);
	if (IS_ERR(fc)) {
		folio_put(ctx->bh_folio);
		ctx->bh_folio = NULL;
		free_percpu(ctx->bdev.bd_stats);
		ctx->bdev.bd_stats = NULL;
		KUNIT_FAIL_AND_ABORT(test, "fs_context_for_mount failed: %ld",
				     PTR_ERR(fc));
	}
	sb = sget_fc(fc, NULL, migrate_test_set_super);
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
	INIT_WORK(&sbi->s_sb_upd_work, migrate_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, migrate_test_err_timer, 0);
	up_write(&sb->s_umount);

	ret = kunit_add_action_or_reset(test, migrate_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sb->s_fs_info = sbi;
	sb->s_op = &migrate_test_sops;
	sb->s_blocksize = MIGRATE_TEST_BLK_SIZE;
	sb->s_blocksize_bits = MIGRATE_TEST_BLK_BITS;
	sb->s_magic = EXT4_SUPER_MAGIC;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	INIT_LIST_HEAD(&sb->s_inodes);
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif

	strscpy(sb->s_id, "ext4_migrate_kunit", sizeof(sb->s_id));
	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = MIGRATE_TEST_BLK_BITS;
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

	sbi->s_blocks_per_group = MIGRATE_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = MIGRATE_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = MIGRATE_TEST_NGROUPS;
	sbi->s_inodes_per_group = MIGRATE_TEST_INODES_PER_GRP;
	sbi->s_inode_size = MIGRATE_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = MIGRATE_TEST_BLK_SIZE /
				  MIGRATE_TEST_INODE_SIZE;
	sbi->s_first_ino = 11;
	sbi->s_mount_state = EXT4_VALID_FS;
	set_opt(sb, ERRORS_CONT);

	sbh = migrate_get_or_create_bh(ctx, MIGRATE_TEST_FIRST_DATA_BLK);
	KUNIT_ASSERT_NOT_NULL(test, sbh);
	brelse(sbh);
	sbi->s_sbh = sbh;
	ctx->es = (struct ext4_super_block *)sbh->b_data;
	sbi->s_es = ctx->es;

	ctx->es->s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es->s_first_data_block = cpu_to_le32(MIGRATE_TEST_FIRST_DATA_BLK);
	ctx->es->s_blocks_per_group = cpu_to_le32(MIGRATE_TEST_BLOCKS_PER_GRP);
	ctx->es->s_clusters_per_group =
		cpu_to_le32(MIGRATE_TEST_BLOCKS_PER_GRP);
	ctx->es->s_inodes_per_group = cpu_to_le32(MIGRATE_TEST_INODES_PER_GRP);
	ctx->es->s_inodes_count = cpu_to_le32(MIGRATE_TEST_NGROUPS *
					      MIGRATE_TEST_INODES_PER_GRP);
	ctx->es->s_inode_size = cpu_to_le16(MIGRATE_TEST_INODE_SIZE);
	ctx->es->s_checksum_type = EXT4_CRC32C_CHKSUM;
	generate_random_uuid(ctx->es->s_uuid);
	ext4_blocks_count_set(ctx->es, MIGRATE_TEST_TOTAL_BLKS);
	ext4_set_feature_extents(sb);
	ext4_set_feature_64bit(sb);
	ext4_set_feature_metadata_csum(sb);
	sbi->s_csum_seed = ext4_chksum(~0, ctx->es->s_uuid,
				       sizeof(ctx->es->s_uuid));
	ext4_superblock_csum_set(sb);

	root_inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, root_inode);
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_blkbits = MIGRATE_TEST_BLK_BITS;
	root_inode->i_flags |= S_NOQUOTA;
	root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	KUNIT_ASSERT_NOT_NULL(test, sb->s_root);
	sb->s_flags |= SB_ACTIVE;

	active_migrate_test = test;
	kunit_activate_static_stub(test, ext4_sb_bread, migrate_sb_bread_stub);
	kunit_activate_static_stub(test, ext4_ext_getblk,
				   migrate_ext_getblk_stub);
	kunit_activate_static_stub(test, __ext4_ext_dirty,
				   migrate_ext_dirty_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   migrate_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   migrate_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, __ext4_new_inode,
				   migrate_new_inode_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   migrate_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   migrate_free_blocks_stub);

	return 0;
}

/*
 * Test 1: ext4_ext_migrate() precondition guards, fast symlink no-op, and
 * temporary inode allocation failure handling.
 */
static void test_ext_migrate_precondition_checks_and_fast_symlink(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode, *symlink;

	inode = migrate_make_test_inode(test, 12, S_IFREG | 0644);

	/* 1. Filesystem lacks extents feature -> -EINVAL */
	ext4_clear_feature_extents(sb);
	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), -EINVAL);
	ext4_set_feature_extents(sb);

	/* 2. Inode already uses extents -> -EINVAL */
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), -EINVAL);
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);

	/* 3. Inline-data inode -> -EINVAL */
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	EXT4_I(inode)->i_inline_off = 164;
	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), -EINVAL);
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	EXT4_I(inode)->i_inline_off = 0;

	/* 4. Fast symlink (S_IFLNK && i_blocks == 0) is a clean no-op */
	symlink = migrate_make_test_inode(test, 13, S_IFLNK | 0777);
	symlink->i_blocks = 0;
	memcpy(EXT4_I(symlink)->i_data, "target/path", 11);
	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(symlink), 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(symlink, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_MEMEQ(test, EXT4_I(symlink)->i_data, "target/path", 11);

	/* 5. Temporary inode allocation failure (-ENOSPC) propagates */
	ctx->new_inode_err = -ENOSPC;
	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), -ENOSPC);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	ctx->new_inode_err = 0;
}

/*
 * Test 2: ext4_ext_migrate() coalesces contiguous direct + single-indirect
 * runs across the EXT4_NDIR_BLOCKS boundary, preserves sparse holes, and
 * frees the indirect metadata block.
 */
static void test_ext_migrate_direct_and_ind_coalescing_and_holes(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct inode *inode;
	struct buffer_head *ind_bh;
	__le32 *ind_data;
	struct ext4_extent_header *eh;
	struct ext4_extent *ex;

	inode = migrate_make_test_inode(test, 14, S_IFREG | 0644);
	i_size_write(inode, 25 * MIGRATE_TEST_BLK_SIZE);
	inode->i_blocks = 8 * (MIGRATE_TEST_BLK_SIZE >> 9);

	/* Direct [0..2] -> 100..102; hole [3..10]; Direct [11] -> 200 */
	EXT4_I(inode)->i_data[0] = cpu_to_le32(100);
	EXT4_I(inode)->i_data[1] = cpu_to_le32(101);
	EXT4_I(inode)->i_data[2] = cpu_to_le32(102);
	EXT4_I(inode)->i_data[11] = cpu_to_le32(200);

	/* IND block 500: [0..1] (lblks 12..13) -> 201..202; [10] (lblk 22) -> 350 */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(500);
	ind_bh = migrate_get_or_create_bh(ctx, 500);
	KUNIT_ASSERT_NOT_NULL(test, ind_bh);
	ind_data = (__le32 *)ind_bh->b_data;
	ind_data[0] = cpu_to_le32(201);
	ind_data[1] = cpu_to_le32(202);
	ind_data[10] = cpu_to_le32(350);
	brelse(ind_bh);

	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));

	eh = ext_inode_hdr(inode);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_magic),
			le16_to_cpu(EXT4_EXT_MAGIC));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_depth), 0U);
	KUNIT_ASSERT_EQ(test, le16_to_cpu(eh->eh_entries), 3U);

	ex = EXT_FIRST_EXTENT(eh);
	/* Extent 0: lblk [0..2] -> pblk 100 */
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 0U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ex[0].ee_len), 3U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[0]), 100ULL);

	/* Extent 1: lblk [11..13] coalesced across DIRECT/IND -> pblk 200 */
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[1].ee_block), 11U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ex[1].ee_len), 3U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[1]), 200ULL);

	/* Extent 2: lblk [22..22] -> pblk 350 */
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[2].ee_block), 22U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ex[2].ee_len), 1U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[2]), 350ULL);

	/* Indirect metadata block 500 was freed */
	KUNIT_EXPECT_TRUE(test, migrate_was_freed(ctx, 500));
}

/*
 * Test 3: ext4_ext_migrate() across DIRECT, IND, DIND, and TIND sparse blocks
 * (> 4 discontiguous extents, growing a depth-1 extent tree) and freeing all
 * indirect metadata blocks at every level.
 */
static void test_ext_migrate_dind_tind_and_multilevel_tree(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct inode *inode;
	struct buffer_head *bh;
	struct ext4_ext_path *path;
	static const struct {
		ext4_lblk_t lblk;
		ext4_fsblk_t pblk;
	} expected[] = {
		{ 0, 100 },
		{ 5, 110 },
		{ 15, 120 },
		{ 780, 130 },
		{ 131341, 140 },
		{ 131345, 150 },
	};
	static const ext4_fsblk_t expected_freed_meta[] = {
		510, 520, 521, 530, 531, 532
	};
	unsigned int i;

	inode = migrate_make_test_inode(test, 15, S_IFREG | 0644);
	i_size_write(inode, 140000ULL * MIGRATE_TEST_BLK_SIZE);
	inode->i_blocks = 12 * (MIGRATE_TEST_BLK_SIZE >> 9);

	/* Direct: lblk 0 -> 100, lblk 5 -> 110 */
	EXT4_I(inode)->i_data[0] = cpu_to_le32(100);
	EXT4_I(inode)->i_data[5] = cpu_to_le32(110);

	/* IND (510): entry 3 (lblk 12 + 3 = 15) -> 120 */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(510);
	bh = migrate_get_or_create_bh(ctx, 510);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[3] = cpu_to_le32(120);
	brelse(bh);

	/* DIND (520) -> [2] = 521 -> [0] (lblk 12 + 256 + 512 = 780) -> 130 */
	EXT4_I(inode)->i_data[EXT4_DIND_BLOCK] = cpu_to_le32(520);
	bh = migrate_get_or_create_bh(ctx, 520);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[2] = cpu_to_le32(521);
	brelse(bh);

	bh = migrate_get_or_create_bh(ctx, 521);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[0] = cpu_to_le32(130);
	brelse(bh);

	/*
	 * TIND (530) -> [1] = 531 -> [0] = 532 -> [1] (lblk 12 + 256 + 65536 +
	 * 65536 + 1 = 131341 -> 140) and [5] (lblk 131345 -> 150).
	 */
	EXT4_I(inode)->i_data[EXT4_TIND_BLOCK] = cpu_to_le32(530);
	bh = migrate_get_or_create_bh(ctx, 530);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[1] = cpu_to_le32(531);
	brelse(bh);

	bh = migrate_get_or_create_bh(ctx, 531);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[0] = cpu_to_le32(532);
	brelse(bh);

	bh = migrate_get_or_create_bh(ctx, 532);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	((__le32 *)bh->b_data)[1] = cpu_to_le32(140);
	((__le32 *)bh->b_data)[5] = cpu_to_le32(150);
	brelse(bh);

	KUNIT_EXPECT_EQ(test, ext4_ext_migrate(inode), 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(inode)->eh_depth), 1U);

	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		struct ext4_extent ex;

		path = ext4_find_extent(inode, expected[i].lblk, NULL,
					EXT4_EX_FORCE_CACHE);
		KUNIT_ASSERT_FALSE_MSG(test, IS_ERR(path),
				       "find_extent failed at idx %u", i);
		if (!path[path->p_depth].p_ext) {
			ext4_free_ext_path(path);
			KUNIT_FAIL_AND_ABORT(test,
					     "missing extent at idx %u", i);
		}
		ex = *path[path->p_depth].p_ext;
		ext4_free_ext_path(path);
		KUNIT_EXPECT_EQ_MSG(test, le32_to_cpu(ex.ee_block),
				    expected[i].lblk, "lblk idx %u", i);
		KUNIT_EXPECT_EQ_MSG(test, ext4_ext_pblock(&ex),
				    expected[i].pblk, "pblk idx %u", i);
	}

	for (i = 0; i < ARRAY_SIZE(expected_freed_meta); i++) {
		KUNIT_EXPECT_TRUE_MSG(test,
				      migrate_was_freed(ctx,
							expected_freed_meta[i]),
				      "expected meta block %llu freed",
				      expected_freed_meta[i]);
	}
}

/*
 * Test 4: When reading an indirect block fails midway after tmp_inode has
 * already grown a depth-1 extent tree, free_ext_block() / free_ext_idx()
 * frees the allocated temporary extent metadata block and leaves the original
 * inode untouched.
 */
static void test_ext_migrate_error_rollback_frees_tmp_extent_blocks(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct inode *inode;
	ext4_fsblk_t expected_leaf_blk = ctx->next_alloc_pblk;

	inode = migrate_make_test_inode(test, 16, S_IFREG | 0644);
	inode->i_blocks = 12;

	/*
	 * 6 discontiguous direct blocks flush 5 extents into tmp_inode
	 * (the 6th stays buffered in lb), growing tmp_inode past eh_max=4
	 * to a depth-1 extent leaf block before EXT4_IND_BLOCK is read.
	 */
	EXT4_I(inode)->i_data[0] = cpu_to_le32(100);
	EXT4_I(inode)->i_data[2] = cpu_to_le32(110);
	EXT4_I(inode)->i_data[4] = cpu_to_le32(120);
	EXT4_I(inode)->i_data[6] = cpu_to_le32(130);
	EXT4_I(inode)->i_data[8] = cpu_to_le32(140);
	EXT4_I(inode)->i_data[10] = cpu_to_le32(150);
	/* Unreadable IND block beyond device_blocks triggers -EIO rollback */
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] =
		cpu_to_le32(MIGRATE_TEST_TOTAL_BLKS + 50);

	(void)ext4_ext_migrate(inode);

	/* Temporary extent leaf block allocated during build must be freed */
	KUNIT_EXPECT_TRUE(test, migrate_was_freed(ctx, expected_leaf_blk));
	/* Original inode remains indirect with intact direct pointers */
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[0]), 100U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[8]), 140U);
}

/*
 * Test 5: ext4_ext_swap_inode_data() returns -EAGAIN without modifying the
 * target inode or freeing its indirect blocks if EXT4_STATE_EXT_MIGRATE was
 * cleared by a concurrent allocation.
 */
static void test_ext_swap_inode_data_detects_concurrent_allocation(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct inode *inode, *tmp_inode;
	handle_t *handle;
	int ret;

	inode = migrate_make_test_inode(test, 17, S_IFREG | 0644);
	EXT4_I(inode)->i_data[0] = cpu_to_le32(210);
	EXT4_I(inode)->i_data[EXT4_IND_BLOCK] = cpu_to_le32(550);

	tmp_inode = migrate_make_test_inode(test, 18, S_IFREG | 0644);
	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ext4_ext_tree_init(handle, tmp_inode);

	/* EXT4_STATE_EXT_MIGRATE intentionally NOT set on inode */
	ret = ext4_ext_swap_inode_data_test(handle, inode, tmp_inode);
	ext4_journal_stop(handle);

	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[0]), 210U);
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(EXT4_I(inode)->i_data[EXT4_IND_BLOCK]),
			550U);
	KUNIT_EXPECT_FALSE(test, migrate_was_freed(ctx, 550));
}

/*
 * Test 6: ext4_ind_migrate() converts an empty 0-extent file and a 1-extent
 * file with a leading sparse hole ([3..7] -> 200..204) to direct block mapping.
 */
static void test_ind_migrate_empty_and_single_direct_extent(struct kunit *test)
{
	struct inode *inode;
	struct ext4_extent_header *eh;
	struct ext4_extent *ex;
	handle_t *handle;
	unsigned int i;

	/* 1. Empty 0-extent file */
	inode = migrate_make_test_inode(test, 19, S_IFREG | 0644);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ext4_ext_tree_init(handle, inode);
	ext4_journal_stop(handle);

	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	for (i = 0; i < EXT4_N_BLOCKS; i++)
		KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[i]), 0U);

	/* 2. Single extent covering logical blocks [3..7] -> physical 200..204 */
	inode = migrate_make_test_inode(test, 20, S_IFREG | 0644);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ext4_ext_tree_init(handle, inode);
	ext4_journal_stop(handle);

	eh = ext_inode_hdr(inode);
	eh->eh_entries = cpu_to_le16(1);
	ex = EXT_FIRST_EXTENT(eh);
	ex->ee_block = cpu_to_le32(3);
	ex->ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(ex, 200);

	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	for (i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[i]), 0U);
	for (i = 3; i <= 7; i++)
		KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[i]),
				200U + (i - 3));
	for (i = 8; i < EXT4_N_BLOCKS; i++)
		KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT4_I(inode)->i_data[i]), 0U);
}

/*
 * Test 7: ext4_ind_migrate() rejects non-extents, bigalloc, multi-extent,
 * non-zero depth, >= EXT4_NDIR_BLOCKS logical range, and >32-bit filesystems.
 */
static void test_ind_migrate_rejections(struct kunit *test)
{
	struct migrate_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode;
	struct ext4_extent_header *eh;
	struct ext4_extent *ex;
	handle_t *handle;

	inode = migrate_make_test_inode(test, 21, S_IFREG | 0644);

	/* 1. Inode without EXT4_INODE_EXTENTS -> -EINVAL */
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EINVAL);

	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
	handle = ext4_journal_start(inode, EXT4_HT_MIGRATE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ext4_ext_tree_init(handle, inode);
	ext4_journal_stop(handle);

	/* 2. Filesystem without extents feature -> -EINVAL */
	ext4_clear_feature_extents(sb);
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EINVAL);
	ext4_set_feature_extents(sb);

	/* 3. bigalloc enabled -> -EOPNOTSUPP */
	ext4_set_feature_bigalloc(sb);
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EOPNOTSUPP);
	ext4_clear_feature_bigalloc(sb);

	/* 4. Extent reaching EXT4_NDIR_BLOCKS (end = 10 + 3 - 1 = 12) */
	eh = ext_inode_hdr(inode);
	eh->eh_entries = cpu_to_le16(1);
	ex = EXT_FIRST_EXTENT(eh);
	ex[0].ee_block = cpu_to_le32(10);
	ex[0].ee_len = cpu_to_le16(3);
	ext4_ext_store_pblock(&ex[0], 200);
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EOPNOTSUPP);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));

	/* 5. More than 1 extent (eh_entries == 2) -> -EOPNOTSUPP */
	ex[0].ee_block = cpu_to_le32(0);
	ex[0].ee_len = cpu_to_le16(2);
	ext4_ext_store_pblock(&ex[0], 200);
	ex[1].ee_block = cpu_to_le32(4);
	ex[1].ee_len = cpu_to_le16(2);
	ext4_ext_store_pblock(&ex[1], 210);
	eh->eh_entries = cpu_to_le16(2);
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EOPNOTSUPP);

	/* 6. Filesystem > EXT4_MAX_BLOCK_FILE_PHYS -> -EOPNOTSUPP */
	eh->eh_entries = cpu_to_le16(1);
	ext4_blocks_count_set(ctx->es, (ext4_fsblk_t)EXT4_MAX_BLOCK_FILE_PHYS +
				       100ULL);
	KUNIT_EXPECT_EQ(test, ext4_ind_migrate(inode), -EOPNOTSUPP);
	ext4_blocks_count_set(ctx->es, MIGRATE_TEST_TOTAL_BLKS);
}

static struct kunit_case ext4_migrate_test_cases[] = {
	KUNIT_CASE(test_ext_migrate_precondition_checks_and_fast_symlink),
	KUNIT_CASE(test_ext_migrate_direct_and_ind_coalescing_and_holes),
	KUNIT_CASE(test_ext_migrate_dind_tind_and_multilevel_tree),
	KUNIT_CASE(test_ext_migrate_error_rollback_frees_tmp_extent_blocks),
	KUNIT_CASE(test_ext_swap_inode_data_detects_concurrent_allocation),
	KUNIT_CASE(test_ind_migrate_empty_and_single_direct_extent),
	KUNIT_CASE(test_ind_migrate_rejections),
	{}
};

static struct kunit_suite ext4_migrate_test_suite = {
	.name = "ext4_migrate_test",
	.init = migrate_test_init,
	.test_cases = ext4_migrate_test_cases,
};

kunit_test_suites(&ext4_migrate_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 indirect/extent inode migration");
MODULE_LICENSE("GPL");
