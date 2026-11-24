// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/super.c
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"

#define SUPER_TEST_NGROUPS		4U
#define SUPER_TEST_BLK_SIZE		1024U
#define SUPER_TEST_BLK_BITS		10U
#define SUPER_TEST_BLOCKS_PER_GRP	1024U
#define SUPER_TEST_FIRST_DATA_BLK	1U
#define SUPER_TEST_TOTAL_BLKS		(SUPER_TEST_FIRST_DATA_BLK + \
					 SUPER_TEST_NGROUPS * \
					 SUPER_TEST_BLOCKS_PER_GRP)
#define SUPER_TEST_INODE_SIZE		256U
#define SUPER_TEST_EXTRA_ISIZE		32U
#define SUPER_TEST_INODES_PER_BLK	(SUPER_TEST_BLK_SIZE / \
					 SUPER_TEST_INODE_SIZE)
#define SUPER_TEST_INODES_PER_GRP	32U
#define SUPER_TEST_ITB_PER_GRP		(SUPER_TEST_INODES_PER_GRP / \
					 SUPER_TEST_INODES_PER_BLK)
#define SUPER_TEST_TOTAL_INODES		(SUPER_TEST_NGROUPS * \
					 SUPER_TEST_INODES_PER_GRP)
#define SUPER_TEST_FIRST_INO		11U
#define SUPER_TEST_GDB_COUNT		1U
#define SUPER_TEST_RSV_GDT_BLKS		2U

struct super_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct buffer_head sb_bh;
	struct ext4_group_desc gdps[8];
	journal_t mock_journal;
	struct inode *journal_inode;
	bool counters_inited;
};

static struct kunit *active_super_test;

static struct inode *super_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = SUPER_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void super_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void super_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **super_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations super_test_sops = {
	.alloc_inode	= super_test_alloc_inode,
	.free_inode	= super_test_free_inode,
	.evict_inode	= super_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= super_test_get_dquots,
#endif
};

static void super_test_sb_upd_work(struct work_struct *work)
{
}

static void super_test_err_timer(struct timer_list *t)
{
}

static int super_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type super_test_fs_type = {
	.name		 = "ext4_super_kunit",
	.init_fs_context = super_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static inline ext4_fsblk_t super_group_first_blk(ext4_group_t g)
{
	return SUPER_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * SUPER_TEST_BLOCKS_PER_GRP;
}

static inline unsigned int super_default_base_meta(ext4_group_t g)
{
	if (g == 2)
		return 0;
	return 1U + SUPER_TEST_GDB_COUNT + SUPER_TEST_RSV_GDT_BLKS;
}

static void super_setup_default_groups(struct super_test_ctx *ctx)
{
	struct super_block *sb = ctx->sb;
	ext4_group_t g;

	memset(ctx->gdps, 0, sizeof(ctx->gdps));
	for (g = 0; g < SUPER_TEST_NGROUPS; g++) {
		ext4_fsblk_t base = super_group_first_blk(g) +
				    super_default_base_meta(g);

		ext4_block_bitmap_set(sb, &ctx->gdps[g], base);
		ext4_inode_bitmap_set(sb, &ctx->gdps[g], base + 1);
		ext4_inode_table_set(sb, &ctx->gdps[g], base + 2);
		ctx->gdps[g].bg_flags = cpu_to_le16(EXT4_BG_INODE_ZEROED);
		ext4_group_desc_csum_set(sb, g, &ctx->gdps[g]);
	}
}

static struct ext4_group_desc *
super_get_group_desc_stub(struct super_block *sb, ext4_group_t block_group,
			  struct buffer_head **bh)
{
	struct kunit *test = active_super_test;
	struct super_test_ctx *ctx;

	if (!test)
		return NULL;
	ctx = test->priv;
	if (!ctx || sb != ctx->sb || block_group >= ARRAY_SIZE(ctx->gdps))
		return NULL;
	if (bh)
		*bh = NULL;
	return &ctx->gdps[block_group];
}

static struct inode *super_iget_stub(struct super_block *sb, unsigned long ino,
				     ext4_iget_flags flags,
				     const char *function, unsigned int line)
{
	struct kunit *test = active_super_test;
	struct super_test_ctx *ctx;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb || !ctx->journal_inode ||
	    ctx->journal_inode->i_ino != ino)
		return ERR_PTR(-ENOENT);

	ihold(ctx->journal_inode);
	return ctx->journal_inode;
}

static void super_test_cleanup(struct super_test_ctx *ctx)
{
#ifdef CONFIG_QUOTA
	int i;
#endif

	active_super_test = NULL;
	ctx->sbi.s_journal = NULL;

	if (ctx->journal_inode) {
		iput(ctx->journal_inode);
		ctx->journal_inode = NULL;
	}

	ext4_flex_groups_free_test(&ctx->sbi);

#ifdef CONFIG_QUOTA
	for (i = 0; i < EXT4_MAXQUOTAS; i++) {
		kfree(rcu_access_pointer(ctx->sbi.s_qf_names[i]));
		rcu_assign_pointer(ctx->sbi.s_qf_names[i], NULL);
	}
#endif
	fscrypt_free_dummy_policy(&ctx->sbi.s_dummy_enc_policy);

	deactivate_super(ctx->sb);
	rcu_barrier();

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);

	if (ctx->counters_inited) {
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
		percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
		percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
	}
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(super_test_cleanup_action, super_test_cleanup,
			    struct super_test_ctx *);

static int super_test_init(struct kunit *test)
{
	struct super_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;
	active_super_test = test;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	if (!ctx->bdev.bd_stats) {
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bd_stats");
	}

	fc = fs_context_for_mount(&super_test_fs_type, 0);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	sb = sget_fc(fc, NULL, set_anon_super_fc);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	ctx->sb = sb;
	sbi = &ctx->sbi;

	err = percpu_counter_init(&sbi->s_freeclusters_counter, 0, GFP_KERNEL);
	if (err)
		goto err_sb;
	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		goto err_sb;
	}
	err = percpu_counter_init(&sbi->s_freeinodes_counter, 0, GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
		percpu_counter_destroy(&sbi->s_freeclusters_counter);
		goto err_sb;
	}
	ctx->counters_inited = true;

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &super_test_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = SUPER_TEST_BLK_SIZE;
	sb->s_blocksize_bits = SUPER_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_super_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = SUPER_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev_disk.part0 = &ctx->bdev;
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(SUPER_TEST_FIRST_DATA_BLK);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	ctx->es.s_inodes_count = cpu_to_le32(SUPER_TEST_TOTAL_INODES);
	ctx->es.s_inodes_per_group = cpu_to_le32(SUPER_TEST_INODES_PER_GRP);
	ctx->es.s_blocks_per_group = cpu_to_le32(SUPER_TEST_BLOCKS_PER_GRP);
	ctx->es.s_clusters_per_group = cpu_to_le32(SUPER_TEST_BLOCKS_PER_GRP);
	ctx->es.s_first_ino = cpu_to_le32(SUPER_TEST_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(SUPER_TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(SUPER_TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(SUPER_TEST_EXTRA_ISIZE);
	ctx->es.s_desc_size = cpu_to_le16(EXT4_MIN_DESC_SIZE_64BIT);
	ctx->es.s_reserved_gdt_blocks = cpu_to_le16(SUPER_TEST_RSV_GDT_BLKS);
	ctx->es.s_errors = cpu_to_le16(EXT4_ERRORS_RO);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	generate_random_uuid(ctx->es.s_uuid);
	ext4_blocks_count_set(&ctx->es, SUPER_TEST_TOTAL_BLKS);

	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_extents(sb);
	ext4_set_feature_extra_isize(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);
	sbi->s_csum_seed = ext4_chksum(~0, ctx->es.s_uuid,
				       sizeof(ctx->es.s_uuid));
	ext4_superblock_csum_set(sb);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	sbi->s_sb_block = 1;
	sbi->s_inode_size = SUPER_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = SUPER_TEST_INODES_PER_BLK;
	sbi->s_inodes_per_group = SUPER_TEST_INODES_PER_GRP;
	sbi->s_itb_per_group = SUPER_TEST_ITB_PER_GRP;
	sbi->s_first_ino = SUPER_TEST_FIRST_INO;
	sbi->s_want_extra_isize = SUPER_TEST_EXTRA_ISIZE;
	sbi->s_blocks_per_group = SUPER_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = SUPER_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = SUPER_TEST_NGROUPS;
	sbi->s_gdb_count = SUPER_TEST_GDB_COUNT;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = SUPER_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block_bits = ilog2(sbi->s_desc_per_block);
	sbi->s_resuid = make_kuid(&init_user_ns, EXT4_DEF_RESUID);
	sbi->s_resgid = make_kgid(&init_user_ns, EXT4_DEF_RESGID);
	sbi->s_commit_interval = JBD2_DEFAULT_MAX_COMMIT_AGE * HZ;
	sbi->s_min_batch_time = EXT4_DEF_MIN_BATCH_TIME;
	sbi->s_max_batch_time = EXT4_DEF_MAX_BATCH_TIME;
	sbi->s_inode_readahead_blks = EXT4_DEF_INODE_READAHEAD_BLKS;
	sbi->s_li_wait_mult = EXT4_DEF_LI_WAIT_MULT;
	sbi->s_err_report_sec = 86400;

	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	INIT_WORK(&sbi->s_sb_upd_work, super_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, super_test_err_timer, 0);

	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	super_setup_default_groups(ctx);

	root_inode = new_inode(sb);
	if (!root_inode) {
		err = -ENOMEM;
		goto err_counters;
	}
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_flags |= S_NOQUOTA;
	root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto err_counters;
	}

	up_write(&sb->s_umount);

	kunit_activate_static_stub(test, ext4_get_group_desc,
				   super_get_group_desc_stub);
	kunit_activate_static_stub(test, __ext4_iget, super_iget_stub);

	err = kunit_add_action_or_reset(test, super_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	return 0;

err_counters:
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	percpu_counter_destroy(&sbi->s_freeclusters_counter);
err_sb:
	active_super_test = NULL;
	deactivate_locked_super(sb);
	rcu_barrier();
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	return err;
}

static void test_feature_set_and_compatibility(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_super_block *es = &ctx->es;
	__le32 orig_incompat = es->s_feature_incompat;
	__le32 orig_ro_compat = es->s_feature_ro_compat;

	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 1);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			0);

	/* Unknown incompat flag must fail on both RW and RO */
	es->s_feature_incompat = orig_incompat | cpu_to_le32(0x80000000U);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 0);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);
	es->s_feature_incompat = orig_incompat;

	/* Unknown ro_compat flag fails RW, succeeds RO */
	es->s_feature_ro_compat = orig_ro_compat | cpu_to_le32(0x80000000U);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 0);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 1), 1);
	es->s_feature_ro_compat = orig_ro_compat;

	/* RO_COMPAT_READONLY forces SB_RDONLY on RW mount request */
	ext4_set_feature_readonly(sb);
	sb->s_flags &= ~SB_RDONLY;
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 1);
	KUNIT_EXPECT_TRUE(test, sb_rdonly(sb));
	sb->s_flags &= ~SB_RDONLY;
	es->s_feature_ro_compat = orig_ro_compat;

	/* bigalloc requires extents and s_first_data_block == 0 on RW */
	ext4_set_feature_bigalloc(sb);
	ext4_clear_feature_extents(sb);
	es->s_first_data_block = 0;
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 0);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 1), 1);

	ext4_set_feature_extents(sb);
	es->s_first_data_block = cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 0);
	es->s_first_data_block = 0;
	KUNIT_EXPECT_EQ(test, ext4_feature_set_ok(sb, 0), 1);
	es->s_first_data_block = cpu_to_le32(SUPER_TEST_FIRST_DATA_BLK);
	es->s_feature_ro_compat = orig_ro_compat;
	es->s_feature_incompat = orig_incompat;

	/* Hurd creator OS sets HURD_COMPAT and rejects 64bit and ea_inode */
	es->s_creator_os = cpu_to_le32(EXT4_OS_HURD);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);
	KUNIT_EXPECT_NE(test, test_opt2(sb, HURD_COMPAT), 0);

	ext4_clear_feature_64bit(sb);
	ext4_set_feature_ea_inode(sb);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);

	ext4_clear_feature_ea_inode(sb);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			0);
	clear_opt2(sb, HURD_COMPAT);
	es->s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	es->s_feature_incompat = orig_incompat;

#ifdef CONFIG_FS_DAX
	/* DAX_ALWAYS rejects inline_data and non-DAX block devices */
	set_opt(sb, DAX_ALWAYS);
	ext4_set_feature_inline_data(sb);
	set_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);

	ext4_clear_feature_inline_data(sb);
	clear_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);

	set_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			0);
	clear_bit(EXT4_FLAGS_BDEV_IS_DAX, &ctx->sbi.s_ext4_flags);
	clear_opt(sb, DAX_ALWAYS);
#endif

	/* Non-zero s_encryption_level with encrypt feature is rejected */
	ext4_set_feature_encrypt(sb);
	es->s_encryption_level = 1;
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			-EINVAL);
	es->s_encryption_level = 0;
	KUNIT_EXPECT_EQ(test, ext4_check_feature_compatibility_test(sb, es, 0),
			0);
	ext4_clear_feature_encrypt(sb);
}

static void test_inode_info_init(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;

	/* EXT4_GOOD_OLD_REV forces 128-byte inodes and first_ino == 11 */
	es->s_rev_level = cpu_to_le32(EXT4_GOOD_OLD_REV);
	es->s_inode_size = cpu_to_le16(512);
	es->s_first_ino = cpu_to_le32(99);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_inode_size, EXT4_GOOD_OLD_INODE_SIZE);
	KUNIT_EXPECT_EQ(test, sbi->s_first_ino,
			(unsigned int)EXT4_GOOD_OLD_FIRST_INO);

	/* EXT4_DYNAMIC_REV validation of s_first_ino and s_inode_size */
	es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	es->s_first_ino = cpu_to_le32(10);
	es->s_inode_size = cpu_to_le16(256);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	es->s_first_ino = cpu_to_le32(11);
	es->s_inode_size = cpu_to_le16(64);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	es->s_inode_size = cpu_to_le16(192);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	es->s_inode_size = cpu_to_le16(2048);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	/* 128-byte inode uses 1-second timestamp granularity */
	es->s_inode_size = cpu_to_le16(128);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sb->s_time_gran, (u32)NSEC_PER_SEC);
	KUNIT_EXPECT_EQ(test, sb->s_time_max, EXT4_NON_EXTRA_TIMESTAMP_MAX);
	KUNIT_EXPECT_EQ(test, sb->s_time_min, EXT4_TIMESTAMP_MIN);

	/* 256-byte inode enables nanosecond extra timestamps and extra_isize */
	es->s_inode_size = cpu_to_le16(256);
	es->s_want_extra_isize = cpu_to_le16(200);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	es->s_want_extra_isize = cpu_to_le16(32);
	es->s_min_extra_isize = cpu_to_le16(200);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), -EINVAL);

	es->s_min_extra_isize = cpu_to_le16(64);
	KUNIT_EXPECT_EQ(test, ext4_inode_info_init_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sb->s_time_gran, 1U);
	KUNIT_EXPECT_EQ(test, sb->s_time_max, EXT4_EXTRA_TIMESTAMP_MAX);
	KUNIT_EXPECT_EQ(test, sbi->s_want_extra_isize, 64U);
}

static void test_block_group_meta_and_clustersize(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;

	/* Without 64bit, descriptor size is forced to 32 bytes */
	ext4_clear_feature_64bit(sb);
	es->s_desc_size = cpu_to_le16(128);
	es->s_state = cpu_to_le16(EXT4_VALID_FS | EXT4_FC_REPLAY);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_desc_size,
			(unsigned long)EXT4_MIN_DESC_SIZE);
	KUNIT_EXPECT_EQ(test, sbi->s_mount_state,
			(unsigned short)EXT4_VALID_FS);

	/* With 64bit, s_desc_size must be a power of 2 in [64, 1024] */
	ext4_set_feature_64bit(sb);
	es->s_desc_size = cpu_to_le16(32);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);
	es->s_desc_size = cpu_to_le16(96);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);
	es->s_desc_size = cpu_to_le16(2048);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);

	es->s_desc_size = cpu_to_le16(64);
	es->s_blocks_per_group = 0;
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);
	es->s_blocks_per_group = cpu_to_le32(SUPER_TEST_BLOCKS_PER_GRP);

	/* Invalid s_inodes_per_group bounds and alignment */
	es->s_inodes_per_group = cpu_to_le32(2);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);
	es->s_inodes_per_group = cpu_to_le32(SUPER_TEST_BLK_SIZE * 8 + 8);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);
	es->s_inodes_per_group = cpu_to_le32(36);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), -EINVAL);

	es->s_inodes_per_group = cpu_to_le32(SUPER_TEST_INODES_PER_GRP);
	KUNIT_EXPECT_EQ(test, ext4_block_group_meta_init_test(sb, 1), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_itb_per_group,
			(unsigned long)SUPER_TEST_ITB_PER_GRP);
	KUNIT_EXPECT_EQ(test, sbi->s_desc_per_block, 16UL);

	/* Non-bigalloc clustersize checks */
	es->s_log_block_size = 0;
	es->s_log_cluster_size = cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	es->s_log_cluster_size = 0;
	sbi->s_blocks_per_group = SUPER_TEST_BLK_SIZE * 8 + 8;
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	sbi->s_blocks_per_group = SUPER_TEST_BLK_SIZE * 8;
	es->s_clusters_per_group = cpu_to_le32(SUPER_TEST_BLK_SIZE * 8 - 4);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	es->s_clusters_per_group = cpu_to_le32(SUPER_TEST_BLK_SIZE * 4);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	clear_opt2(sb, STD_GROUP_SIZE);
	es->s_clusters_per_group = cpu_to_le32(SUPER_TEST_BLK_SIZE * 8);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_cluster_ratio, 1);
	KUNIT_EXPECT_EQ(test, sbi->s_cluster_bits, 0);
	KUNIT_EXPECT_NE(test, test_opt2(sb, STD_GROUP_SIZE), 0);

	/* Bigalloc clustersize checks: 4K blocks, 16K clusters (ratio 4) */
	ext4_set_feature_bigalloc(sb);
	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = 12;
	es->s_log_block_size = cpu_to_le32(2);
	es->s_log_cluster_size = cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	es->s_log_cluster_size = cpu_to_le32(4);
	sbi->s_blocks_per_group = 4096;
	es->s_clusters_per_group = cpu_to_le32(512);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), -EINVAL);

	es->s_clusters_per_group = cpu_to_le32(1024);
	KUNIT_EXPECT_EQ(test, ext4_handle_clustersize_test(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_cluster_bits, 2);
	KUNIT_EXPECT_EQ(test, sbi->s_cluster_ratio, 4);
}

static void test_hash_info_init(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;

	es->s_def_hash_version = DX_HASH_LAST + 1;
	KUNIT_EXPECT_EQ(test, ext4_hash_info_init_test(sb), -EINVAL);

	es->s_def_hash_version = DX_HASH_SIPHASH;
	KUNIT_EXPECT_EQ(test, ext4_hash_info_init_test(sb), -EINVAL);

	es->s_def_hash_version = DX_HASH_HALF_MD4;
	es->s_hash_seed[0] = cpu_to_le32(0x11223344U);
	es->s_hash_seed[1] = cpu_to_le32(0x55667788U);
	es->s_hash_seed[2] = cpu_to_le32(0x99aabbccU);
	es->s_hash_seed[3] = cpu_to_le32(0xddeeff00U);
	ext4_set_feature_dir_index(sb);
	es->s_flags = cpu_to_le32(EXT2_FLAGS_UNSIGNED_HASH);
	sbi->s_hash_unsigned = 0;

	KUNIT_EXPECT_EQ(test, ext4_hash_info_init_test(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_def_hash_version,
			(unsigned char)DX_HASH_HALF_MD4);
	KUNIT_EXPECT_EQ(test, sbi->s_hash_seed[0], 0x11223344U);
	KUNIT_EXPECT_EQ(test, sbi->s_hash_seed[1], 0x55667788U);
	KUNIT_EXPECT_EQ(test, sbi->s_hash_seed[2], 0x99aabbccU);
	KUNIT_EXPECT_EQ(test, sbi->s_hash_seed[3], 0xddeeff00U);
	KUNIT_EXPECT_EQ(test, sbi->s_hash_unsigned, 3);

	/* Unflagged dir_index superblock gets native hash signedness flag */
	es->s_flags = 0;
	sbi->s_hash_unsigned = 0;
	KUNIT_EXPECT_EQ(test, ext4_hash_info_init_test(sb), 0);
	KUNIT_EXPECT_NE(test, le32_to_cpu(es->s_flags) &
			(EXT2_FLAGS_SIGNED_HASH | EXT2_FLAGS_UNSIGNED_HASH),
			0U);
}

static void test_check_geometry(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;

	/* Excessive s_reserved_gdt_blocks (> blocksize / 4) */
	es->s_reserved_gdt_blocks = cpu_to_le16(SUPER_TEST_BLK_SIZE / 4 + 1);
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), -EINVAL);
	es->s_reserved_gdt_blocks = cpu_to_le16(SUPER_TEST_RSV_GDT_BLKS);

	/* Filesystem block count exceeding device capacity */
	ctx->bdev.bd_nr_sectors = 1000;
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), -EINVAL);
	ctx->bdev.bd_nr_sectors = 0;

	/* s_first_data_block >= blocks_count */
	es->s_first_data_block = cpu_to_le32(SUPER_TEST_TOTAL_BLKS);
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), -EINVAL);

	/* s_first_data_block == 0 on 1K blocksize without bigalloc */
	es->s_first_data_block = 0;
	es->s_log_block_size = 0;
	sbi->s_cluster_ratio = 1;
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), -EINVAL);
	es->s_first_data_block = cpu_to_le32(SUPER_TEST_FIRST_DATA_BLK);

	/* Inode count mismatch against groups_count * inodes_per_group */
	es->s_inodes_count = cpu_to_le32(SUPER_TEST_TOTAL_INODES + 1);
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), -EINVAL);

	/* Valid 5-group geometry with partial final group */
	ext4_blocks_count_set(es, SUPER_TEST_FIRST_DATA_BLK +
			      4 * SUPER_TEST_BLOCKS_PER_GRP + 200);
	es->s_inodes_count = cpu_to_le32(5 * SUPER_TEST_INODES_PER_GRP);
	KUNIT_EXPECT_EQ(test, ext4_check_geometry_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_groups_count, 5U);
}

static void test_descriptor_loc(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_super_block *es = &ctx->es;

	/* Non-meta_bg places descriptor blocks linearly after superblock */
	ext4_clear_feature_meta_bg(sb);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 0), 2ULL);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 2), 4ULL);

	/*
	 * With meta_bg and s_desc_per_block == 16 on 1K blocks:
	 *  - nr == 0 -> bg 0 (has_super): block 2 for both first_data_block
	 *    1 and 0 (compensated)
	 *  - nr == 1 -> bg 16 (no backup super under sparse_super): first
	 *    block of group 16
	 *  - nr == 2 -> bg 32 ( still no backup super; 32 is power of 2):
	 *    first block of group 32
	 */
	ext4_set_feature_meta_bg(sb);
	es->s_first_meta_bg = 0;
	es->s_first_data_block = cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 0), 2ULL);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 1),
			ext4_group_first_block_no(sb, 16));

	es->s_first_data_block = 0;
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 0, 0), 2ULL);

	/* Hybrid s_first_meta_bg == 2 uses linear layout for nr < 2 */
	es->s_first_data_block = cpu_to_le32(1);
	es->s_first_meta_bg = cpu_to_le32(2);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 0), 2ULL);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 1), 3ULL);
	KUNIT_EXPECT_EQ(test, ext4_descriptor_loc_test(sb, 1, 2),
			ext4_group_first_block_no(sb, 32));
}

static void test_superblock_and_group_desc_csum(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;
	struct ext4_group_desc gdp = {};

	/* Superblock checksum type, verification, and seed derivation */
	es->s_checksum_type = 99;
	KUNIT_EXPECT_EQ(test, ext4_init_metadata_csum_test(sb, es), -EINVAL);

	es->s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_superblock_csum_set(sb);
	es->s_checksum ^= cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_init_metadata_csum_test(sb, es), -EFSBADCRC);

	ext4_clear_feature_csum_seed(sb);
	ext4_superblock_csum_set(sb);
	sbi->s_csum_seed = 0;
	KUNIT_EXPECT_EQ(test, ext4_init_metadata_csum_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_csum_seed,
			ext4_chksum(~0, es->s_uuid, sizeof(es->s_uuid)));

	ext4_set_feature_csum_seed(sb);
	es->s_checksum_seed = cpu_to_le32(0xdeadbeefU);
	ext4_superblock_csum_set(sb);
	KUNIT_EXPECT_EQ(test, ext4_init_metadata_csum_test(sb, es), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_csum_seed, 0xdeadbeefU);

	/* 32-byte vs 64-byte free_group_clusters accessor/mutator */
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE;
	ext4_free_group_clusters_set(sb, &gdp, 0x12345678U);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, &gdp), 0x5678U);

	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	ext4_free_group_clusters_set(sb, &gdp, 0x12345678U);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, &gdp), 0x12345678U);

	/* metadata_csum (CRC32C) group descriptor verification */
	ext4_group_desc_csum_set(sb, 3, &gdp);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 3, &gdp), 1);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 2, &gdp), 0);
	gdp.bg_block_bitmap_hi = cpu_to_le32(1);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 3, &gdp), 0);

	/* Legacy gdt_csum (CRC16) verification across 64-byte descriptor */
	ext4_clear_feature_metadata_csum(sb);
	ext4_set_feature_gdt_csum(sb);
	ext4_group_desc_csum_set(sb, 1, &gdp);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 1, &gdp), 1);
	gdp.bg_free_blocks_count_hi ^= cpu_to_le16(1);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 1, &gdp), 0);

	/* No checksum features enabled -> verify always succeeds */
	ext4_clear_feature_gdt_csum(sb);
	KUNIT_EXPECT_EQ(test, ext4_group_desc_csum_verify(sb, 1, &gdp), 1);
}

static void test_check_descriptors_and_flex_info(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct flex_groups *fg0, *fg1;
	ext4_group_t first_not_zeroed = 0;

	ctx->gdps[2].bg_flags = 0;
	ext4_group_desc_csum_set(sb, 2, &ctx->gdps[2]);
	KUNIT_EXPECT_EQ(test,
			ext4_check_descriptors_test(sb, 1, &first_not_zeroed),
			1);
	KUNIT_EXPECT_EQ(test, first_not_zeroed, 2U);

	/* Block bitmap overlapping superblock fails RW, tolerated on RO */
	ext4_block_bitmap_set(sb, &ctx->gdps[0], 1);
	ext4_group_desc_csum_set(sb, 0, &ctx->gdps[0]);
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 0);
	sb->s_flags |= SB_RDONLY;
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 1);
	sb->s_flags &= ~SB_RDONLY;

	/* Inode table overlapping GDT block (block 2) fails RW */
	super_setup_default_groups(ctx);
	ext4_inode_table_set(sb, &ctx->gdps[0], 2);
	ext4_group_desc_csum_set(sb, 0, &ctx->gdps[0]);
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 0);

	/* Out-of-group bitmap fails without flex_bg, succeeds with flex_bg */
	super_setup_default_groups(ctx);
	ext4_block_bitmap_set(sb, &ctx->gdps[1], 20);
	ext4_group_desc_csum_set(sb, 1, &ctx->gdps[1]);
	sb->s_flags |= SB_RDONLY;
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 0);
	sb->s_flags &= ~SB_RDONLY;

	ext4_set_feature_flex_bg(sb);
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 1);

	/* Bad group descriptor checksum fails RW, tolerated on RO */
	ctx->gdps[1].bg_checksum ^= cpu_to_le16(0xffff);
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 0);
	sb->s_flags |= SB_RDONLY;
	KUNIT_EXPECT_EQ(test, ext4_check_descriptors_test(sb, 1, NULL), 1);
	sb->s_flags &= ~SB_RDONLY;

	/* Flex-bg allocation and per-flex-group counter aggregation */
	super_setup_default_groups(ctx);
	ctx->es.s_log_groups_per_flex = 32;
	KUNIT_EXPECT_EQ(test, ext4_fill_flex_info_test(sb), 1);
	KUNIT_EXPECT_EQ(test, sbi->s_log_groups_per_flex, 0U);

	ext4_free_group_clusters_set(sb, &ctx->gdps[0], 100);
	ext4_free_inodes_set(sb, &ctx->gdps[0], 10);
	ext4_used_dirs_set(sb, &ctx->gdps[0], 2);
	ext4_free_group_clusters_set(sb, &ctx->gdps[1], 150);
	ext4_free_inodes_set(sb, &ctx->gdps[1], 12);
	ext4_used_dirs_set(sb, &ctx->gdps[1], 3);
	ext4_free_group_clusters_set(sb, &ctx->gdps[2], 200);
	ext4_free_inodes_set(sb, &ctx->gdps[2], 14);
	ext4_used_dirs_set(sb, &ctx->gdps[2], 1);
	ext4_free_group_clusters_set(sb, &ctx->gdps[3], 250);
	ext4_free_inodes_set(sb, &ctx->gdps[3], 16);
	ext4_used_dirs_set(sb, &ctx->gdps[3], 4);

	ctx->es.s_log_groups_per_flex = 1;
	KUNIT_EXPECT_EQ(test, ext4_fill_flex_info_test(sb), 1);
	KUNIT_EXPECT_EQ(test, sbi->s_flex_groups_allocated, 2);

	fg0 = sbi_array_rcu_deref(sbi, s_flex_groups, 0);
	fg1 = sbi_array_rcu_deref(sbi, s_flex_groups, 1);
	KUNIT_EXPECT_EQ(test, atomic64_read(&fg0->free_clusters), 250LL);
	KUNIT_EXPECT_EQ(test, atomic_read(&fg0->free_inodes), 22);
	KUNIT_EXPECT_EQ(test, atomic_read(&fg0->used_dirs), 5);
	KUNIT_EXPECT_EQ(test, atomic64_read(&fg1->free_clusters), 450LL);
	KUNIT_EXPECT_EQ(test, atomic_read(&fg1->free_inodes), 30);
	KUNIT_EXPECT_EQ(test, atomic_read(&fg1->used_dirs), 5);

	/* Growing the flex_bg array preserves populated entries */
	KUNIT_EXPECT_EQ(test, ext4_alloc_flex_bg_array(sb, 8), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_flex_groups_allocated, 4);
	fg0 = sbi_array_rcu_deref(sbi, s_flex_groups, 0);
	KUNIT_EXPECT_EQ(test, atomic64_read(&fg0->free_clusters), 250LL);
}

static void test_max_sizes_stripe_and_resv_clusters(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;

	/* Without huge_file, 2^32 - 1 512B sectors caps max extent file size */
	KUNIT_EXPECT_EQ(test, ext4_max_size_test(10, 0),
			(((1LL << 32) - 1) >> 1) << 10);
	KUNIT_EXPECT_EQ(test, ext4_max_size_test(12, 0),
			(((1LL << 32) - 1) >> 3) << 12);

	/* With huge_file, 32-bit ee_block caps max size at (2^32 - 1) blocks */
	KUNIT_EXPECT_EQ(test, ext4_max_size_test(10, 1),
			((1LL << 32) - 1) << 10);
	KUNIT_EXPECT_EQ(test, ext4_max_size_test(12, 1),
			min_t(loff_t, ((1LL << 32) - 1) << 12,
			      MAX_LFS_FILESIZE));

	/* Indirect bitmap max size is strictly smaller with !huge_file on 4K */
	KUNIT_EXPECT_LT(test, ext4_max_bitmap_size_test(12, 0),
			ext4_max_bitmap_size_test(12, 1));
	KUNIT_EXPECT_EQ(test, ext4_max_bitmap_size_test(10, 1),
			(12LL + 256 + 65536 + 16777216) << 10);

	/* Stripe priority: mount s_stripe -> raid_stripe_width -> stride */
	sbi->s_blocks_per_group = 1024;
	sbi->s_stripe = 64;
	ctx->es.s_raid_stripe_width = cpu_to_le32(128);
	ctx->es.s_raid_stride = cpu_to_le16(16);
	KUNIT_EXPECT_EQ(test, ext4_get_stripe_size_test(sbi), 64UL);

	sbi->s_stripe = 2048;
	KUNIT_EXPECT_EQ(test, ext4_get_stripe_size_test(sbi), 128UL);

	ctx->es.s_raid_stripe_width = cpu_to_le32(2048);
	KUNIT_EXPECT_EQ(test, ext4_get_stripe_size_test(sbi), 16UL);

	ctx->es.s_raid_stride = cpu_to_le16(1);
	KUNIT_EXPECT_EQ(test, ext4_get_stripe_size_test(sbi), 0UL);

	/* Reserved clusters: skipped without extents, 2% up to 4096 cap */
	atomic64_set(&sbi->s_resv_clusters, 7);
	ext4_clear_feature_extents(sb);
	ext4_blocks_count_set(&ctx->es, 5000);
	ext4_set_resv_clusters_test(sb);
	KUNIT_EXPECT_EQ(test, atomic64_read(&sbi->s_resv_clusters), 7LL);

	ext4_set_feature_extents(sb);
	ext4_set_resv_clusters_test(sb);
	KUNIT_EXPECT_EQ(test, atomic64_read(&sbi->s_resv_clusters), 100LL);

	ext4_blocks_count_set(&ctx->es, 500000);
	ext4_set_resv_clusters_test(sb);
	KUNIT_EXPECT_EQ(test, atomic64_read(&sbi->s_resv_clusters), 4096LL);
}

static void test_overhead_and_statfs(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct kstatfs st = {};
	char *buf;

	buf = kunit_kzalloc(test, SUPER_TEST_BLK_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	/*
	 * Non-bigalloc per-group overhead:
	 *  - groups 0, 1, 3: 1 (super) + 1 (gdb) + 2 (rsv_gdt) + 8 (itb) + 2
	 *    = 14
	 *  - group 2 (sparse_super): 0 + 0 + 0 + 8 (itb) + 2 = 10
	 * Total with s_first_data_block == 1: 1 + 14 + 14 + 10 + 14 = 53
	 */
	KUNIT_EXPECT_EQ(test, ext4_count_overhead_test(sb, 0, buf), 14);
	KUNIT_EXPECT_EQ(test, ext4_count_overhead_test(sb, 2, buf), 10);
	KUNIT_EXPECT_EQ(test, ext4_calculate_overhead(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_overhead, 53UL);

	/* Loaded internal journal adds j_total_len clusters */
	ctx->mock_journal.j_total_len = 64;
	sbi->s_journal = &ctx->mock_journal;
	KUNIT_EXPECT_EQ(test, ext4_calculate_overhead(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_overhead, 117UL);
	sbi->s_journal = NULL;

	/* Unloaded internal journal falls back to ext4_get_journal_inode() */
	ctx->journal_inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, ctx->journal_inode);
	ctx->journal_inode->i_ino = EXT4_JOURNAL_INO;
	ctx->journal_inode->i_mode = S_IFREG | 0600;
	ctx->journal_inode->i_flags |= S_NOQUOTA;
	set_nlink(ctx->journal_inode, 1);
	i_size_write(ctx->journal_inode, 32 * SUPER_TEST_BLK_SIZE);
	ext4_set_feature_journal(sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	KUNIT_EXPECT_EQ(test, ext4_calculate_overhead(sb), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_overhead, 85UL);
	ext4_clear_feature_journal(sb);
	ctx->es.s_journal_inum = 0;

	/* Bigalloc deduplicates metadata blocks sharing the same cluster */
	ext4_set_feature_bigalloc(sb);
	ctx->es.s_first_data_block = 0;
	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;
	sbi->s_clusters_per_group = SUPER_TEST_BLOCKS_PER_GRP / 4;
	sbi->s_groups_count = 1;
	ext4_block_bitmap_set(sb, &ctx->gdps[0], 2);
	ext4_inode_bitmap_set(sb, &ctx->gdps[0], 3);
	ext4_inode_table_set(sb, &ctx->gdps[0], 4);
	memset(buf, 0, SUPER_TEST_BLK_SIZE);
	/* Cluster 0 holds super+gdb+bbitmap+ibitmap; clusters 1..2 hold itb */
	KUNIT_EXPECT_EQ(test, ext4_count_overhead_test(sb, 0, buf), 3);

	/* Restore 1:1 geometry and verify ext4_statfs accounting & clamping */
	ext4_clear_feature_bigalloc(sb);
	ctx->es.s_first_data_block = cpu_to_le32(SUPER_TEST_FIRST_DATA_BLK);
	sbi->s_cluster_bits = 0;
	sbi->s_cluster_ratio = 1;
	sbi->s_clusters_per_group = SUPER_TEST_BLOCKS_PER_GRP;
	sbi->s_groups_count = SUPER_TEST_NGROUPS;
	sbi->s_overhead = 53;
	ext4_r_blocks_count_set(&ctx->es, 100);
	atomic64_set(&sbi->s_resv_clusters, 20);
	percpu_counter_set(&sbi->s_freeclusters_counter, 500);
	percpu_counter_set(&sbi->s_dirtyclusters_counter, 50);
	percpu_counter_set(&sbi->s_freeinodes_counter, 90);

	KUNIT_EXPECT_EQ(test, ext4_statfs_test(sb->s_root, &st), 0);
	KUNIT_EXPECT_EQ(test, st.f_type, (long)EXT4_SUPER_MAGIC);
	KUNIT_EXPECT_EQ(test, st.f_blocks, (u64)(SUPER_TEST_TOTAL_BLKS - 53));
	KUNIT_EXPECT_EQ(test, st.f_bfree, 450ULL);
	KUNIT_EXPECT_EQ(test, st.f_bavail, 330ULL);
	KUNIT_EXPECT_EQ(test, st.f_files, (u64)SUPER_TEST_TOTAL_INODES);
	KUNIT_EXPECT_EQ(test, st.f_ffree, 90ULL);
	KUNIT_EXPECT_EQ(test, st.f_namelen, (long)EXT4_NAME_LEN);

	/* MINIX_DF omits s_overhead; dirty > free clamps f_bfree/f_bavail */
	set_opt(sb, MINIX_DF);
	percpu_counter_set(&sbi->s_freeclusters_counter, 30);
	percpu_counter_set(&sbi->s_dirtyclusters_counter, 80);
	KUNIT_EXPECT_EQ(test, ext4_statfs_test(sb->s_root, &st), 0);
	KUNIT_EXPECT_EQ(test, st.f_blocks, (u64)SUPER_TEST_TOTAL_BLKS);
	KUNIT_EXPECT_EQ(test, st.f_bfree, 0ULL);
	KUNIT_EXPECT_EQ(test, st.f_bavail, 0ULL);
}

static void test_mount_options_and_show_options(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;
	unsigned int ioprio = 0;
	struct seq_file seq = {};
	char *out;

	out = kunit_kzalloc(test, 1024, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, out);

	/* Superblock default mount options */
	sbi->s_mount_opt = 0;
	sbi->s_mount_opt2 = 0;
	es->s_default_mount_opts = cpu_to_le32(EXT4_DEFM_DISCARD |
					       EXT4_DEFM_NOBARRIER |
					       EXT4_DEFM_JMODE_WBACK);
	es->s_errors = cpu_to_le16(EXT4_ERRORS_CONTINUE);
	ext4_set_feature_fast_commit(sb);
	ext4_set_def_opts_test(sb, es);

	KUNIT_EXPECT_NE(test, test_opt(sb, DISCARD), 0);
	KUNIT_EXPECT_EQ(test, test_opt(sb, BARRIER), 0);
	KUNIT_EXPECT_EQ(test, test_opt(sb, DATA_FLAGS),
			EXT4_MOUNT_WRITEBACK_DATA);
	KUNIT_EXPECT_NE(test, test_opt(sb, ERRORS_CONT), 0);
	KUNIT_EXPECT_NE(test, test_opt(sb, JOURNAL_CHECKSUM), 0);
	KUNIT_EXPECT_NE(test, test_opt2(sb, JOURNAL_FAST_COMMIT), 0);
	sbi->s_def_mount_opt = sbi->s_mount_opt;
	sbi->s_def_mount_opt2 = sbi->s_mount_opt2;

	/* Valid option string parsing and application */
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"nodelalloc,minixdf,commit=15,stripe=32,errors=panic",
				false),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"resuid=1000,resgid=2000,inode_readahead_blks=64",
				false),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"max_dir_size_kb=512", false),
			0);
	KUNIT_EXPECT_EQ(test, test_opt(sb, DELALLOC), 0);
	KUNIT_EXPECT_NE(test, test_opt(sb, MINIX_DF), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_commit_interval, 15UL * HZ);
	KUNIT_EXPECT_EQ(test, sbi->s_stripe, 32UL);
	KUNIT_EXPECT_EQ(test, from_kuid(&init_user_ns, sbi->s_resuid), 1000U);
	KUNIT_EXPECT_EQ(test, from_kgid(&init_user_ns, sbi->s_resgid), 2000U);
	KUNIT_EXPECT_EQ(test, sbi->s_inode_readahead_blks, 64U);
	KUNIT_EXPECT_EQ(test, sbi->s_max_dir_size_kb, 512U);
	KUNIT_EXPECT_NE(test, test_opt(sb, ERRORS_PANIC), 0);

	/* Invalid parameter values and consistency rejections */
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"inode_readahead_blks=3", false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"mb_optimize_scan=2", false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"debug_want_extra_isize=200", false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"debug_want_extra_isize=5", false),
			-EINVAL);
#ifdef CONFIG_QUOTA
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"prjquota", false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"usrjquota=aquota.user", false),
			-EINVAL);
#endif
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"test_dummy_encryption", false),
			-EINVAL);

	/* Remount restrictions on data= mode changes */
	sbi->s_journal = &ctx->mock_journal;
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"data=ordered", true),
			-EINVAL);
#ifdef CONFIG_FS_DAX
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"dax=never", false),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"dax=always", true),
			-EINVAL);
#else
	KUNIT_EXPECT_EQ(test,
			ext4_parse_and_apply_options_test(sb,
				"dax=never", false),
			-EINVAL);
#endif
	sbi->s_journal = NULL;

	/* On-disk es->s_mount_opts parsing and malformed fallback */
	strscpy_pad(es->s_mount_opts, "nodiscard,commit=25,journal_ioprio=5");
	KUNIT_EXPECT_EQ(test,
			ext4_parse_sb_mount_options_test(sb, NULL, &ioprio),
			0);
	KUNIT_EXPECT_EQ(test, test_opt(sb, DISCARD), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_commit_interval, 25UL * HZ);
	KUNIT_EXPECT_EQ(test, IOPRIO_PRIO_DATA(ioprio), 5UL);

	strscpy_pad(es->s_mount_opts, "nonexistent_opt=1");
	KUNIT_EXPECT_EQ(test,
			ext4_parse_sb_mount_options_test(sb, NULL, NULL), 0);

	/* _ext4_show_options and ext4_seq_options_show formatting */
	seq.buf = out;
	seq.size = 1023;
	seq.private = sb;
	KUNIT_EXPECT_EQ(test, ext4_show_options_test(&seq, sb, 0), 0);
	out[seq.count] = '\0';
	KUNIT_EXPECT_NOT_NULL(test, strstr(out, ",minixdf"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(out, ",commit=25"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(out, ",stripe=32"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(out, ",errors=panic"));

	memset(out, 0, 1024);
	seq.count = 0;
	KUNIT_EXPECT_EQ(test, ext4_seq_options_show(&seq, NULL), 0);
	out[seq.count] = '\0';
	KUNIT_EXPECT_TRUE(test, str_has_prefix(out, "rw\n"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(out, "\nresuid=1000\n"));
}

static void test_error_recording_and_update_super(struct kunit *test)
{
	struct super_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;
	__le32 saved_wtime;

	KUNIT_EXPECT_EQ(test, ext4_errno_to_code_test(EIO), EXT4_ERR_EIO);
	KUNIT_EXPECT_EQ(test, ext4_errno_to_code_test(ENOMEM), EXT4_ERR_ENOMEM);
	KUNIT_EXPECT_EQ(test, ext4_errno_to_code_test(EFSCORRUPTED),
			EXT4_ERR_EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, ext4_errno_to_code_test(ENOSPC), EXT4_ERR_ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_errno_to_code_test(999), EXT4_ERR_UNKNOWN);

	/* First error defaults 0 -> EFSCORRUPTED; second updates last only */
	ext4_save_error_info_test(sb, 0, 17, 1024, "first_fn", 111);
	ext4_save_error_info_test(sb, EIO, 29, 2048, "second_fn", 222);

	KUNIT_EXPECT_EQ(test, sbi->s_add_error_count, 2U);
	KUNIT_EXPECT_EQ(test, sbi->s_first_error_code, EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, sbi->s_last_error_code, EIO);

	sbi->s_kbytes_written = 100;
	percpu_counter_set(&sbi->s_freeclusters_counter, 321);
	percpu_counter_set(&sbi->s_freeinodes_counter, 45);

	ext4_update_super_test(sb);

	KUNIT_EXPECT_EQ(test, sbi->s_add_error_count, 0U);
	KUNIT_EXPECT_NE(test, le16_to_cpu(es->s_state) & EXT4_ERROR_FS, 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_error_count), 2U);
	KUNIT_EXPECT_EQ(test, es->s_first_error_errcode,
			(u8)EXT4_ERR_EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_first_error_ino), 17U);
	KUNIT_EXPECT_EQ(test, le64_to_cpu(es->s_first_error_block), 1024ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_first_error_line), 111U);
	KUNIT_EXPECT_STREQ(test, (char *)es->s_first_error_func, "first_fn");

	KUNIT_EXPECT_EQ(test, es->s_last_error_errcode, (u8)EXT4_ERR_EIO);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_last_error_ino), 29U);
	KUNIT_EXPECT_EQ(test, le64_to_cpu(es->s_last_error_block), 2048ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_last_error_line), 222U);
	KUNIT_EXPECT_STREQ(test, (char *)es->s_last_error_func, "second_fn");

	KUNIT_EXPECT_EQ(test, ext4_free_blocks_count(es), 321ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_free_inodes_count), 45U);
	KUNIT_EXPECT_EQ(test, le64_to_cpu(es->s_kbytes_written), 100ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_checksum),
			le32_to_cpu(ext4_superblock_csum(es)));

	/* Read-only superblock update must not touch s_wtime */
	es->s_wtime = cpu_to_le32(123456U);
	saved_wtime = es->s_wtime;
	sb->s_flags |= SB_RDONLY;
	ext4_update_super_test(sb);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_wtime),
			le32_to_cpu(saved_wtime));
}

static struct kunit_case ext4_super_test_cases[] = {
	KUNIT_CASE(test_feature_set_and_compatibility),
	KUNIT_CASE(test_inode_info_init),
	KUNIT_CASE(test_block_group_meta_and_clustersize),
	KUNIT_CASE(test_hash_info_init),
	KUNIT_CASE(test_check_geometry),
	KUNIT_CASE(test_descriptor_loc),
	KUNIT_CASE(test_superblock_and_group_desc_csum),
	KUNIT_CASE(test_check_descriptors_and_flex_info),
	KUNIT_CASE(test_max_sizes_stripe_and_resv_clusters),
	KUNIT_CASE(test_overhead_and_statfs),
	KUNIT_CASE(test_mount_options_and_show_options),
	KUNIT_CASE(test_error_recording_and_update_super),
	{}
};

static struct kunit_suite ext4_super_test_suite = {
	.name = "ext4_super_test",
	.init = super_test_init,
	.test_cases = ext4_super_test_cases,
};

kunit_test_suites(&ext4_super_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 superblock validation and options");
MODULE_LICENSE("GPL");
