// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/ioctl.c
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/compat.h>
#include <linux/dcache.h>
#include <linux/fileattr.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fscrypt.h>
#include <linux/fsverity.h>
#include <linux/iversion.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mount.h>
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
#include "xattr.h"

#define IOCTL_TEST_NGROUPS		8U
#define IOCTL_TEST_BLK_SIZE		1024U
#define IOCTL_TEST_BLK_BITS		10U
#define IOCTL_TEST_BLOCKS_PER_GRP	1024U
#define IOCTL_TEST_FIRST_DATA_BLK	1U
#define IOCTL_TEST_TOTAL_BLKS		(IOCTL_TEST_FIRST_DATA_BLK + \
					 IOCTL_TEST_NGROUPS * \
					 IOCTL_TEST_BLOCKS_PER_GRP)
#define IOCTL_TEST_INODE_SIZE		256U
#define IOCTL_TEST_EXTRA_ISIZE		32U
#define IOCTL_TEST_INODES_PER_GRP	32U
#define IOCTL_TEST_TOTAL_INODES		(IOCTL_TEST_NGROUPS * \
					 IOCTL_TEST_INODES_PER_GRP)
#define IOCTL_TEST_FIRST_INO		11U

struct ioctl_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block *es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct folio *bh_folio;
	struct buffer_head sb_bh;
	u8 primary_sb_buf[IOCTL_TEST_BLK_SIZE];
	struct buffer_head backup_bhs[IOCTL_TEST_NGROUPS];
	u8 backup_sb_bufs[IOCTL_TEST_NGROUPS][IOCTL_TEST_BLK_SIZE];
	struct buffer_head iloc_bh;
	u8 iloc_buf[IOCTL_TEST_BLK_SIZE];
	int sync_bh_calls;
	int sync_bh_err;
	bool es_shrinker_inited;
};

static struct kunit *active_ioctl_test;

static struct inode *ioctl_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = IOCTL_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void ioctl_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void ioctl_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **ioctl_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations ioctl_test_sops = {
	.alloc_inode	= ioctl_test_alloc_inode,
	.free_inode	= ioctl_test_free_inode,
	.evict_inode	= ioctl_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= ioctl_test_get_dquots,
#endif
};

static void ioctl_test_sb_upd_work(struct work_struct *work)
{
}

static void ioctl_test_err_timer(struct timer_list *t)
{
}

static int ioctl_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type ioctl_test_fs_type = {
	.name		 = "ext4_ioctl_kunit",
	.init_fs_context = ioctl_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static struct buffer_head *ioctl_sb_bread_stub(struct super_block *sb,
					       sector_t block,
					       blk_opf_t op_flags)
{
	struct kunit *test = active_ioctl_test;
	struct ioctl_test_ctx *ctx;
	ext4_group_t g;

	if (!test)
		return ERR_PTR(-EIO);
	ctx = test->priv;
	if (!ctx || sb != ctx->sb)
		return ERR_PTR(-EIO);

	for (g = 0; g < IOCTL_TEST_NGROUPS; g++) {
		ext4_fsblk_t grp_blk = ext4_group_first_block_no(sb, g);

		if ((ext4_fsblk_t)block == grp_blk) {
			struct buffer_head *bh = &ctx->backup_bhs[g];

			get_bh(bh);
			return bh;
		}
	}

	return ERR_PTR(-ENOENT);
}

static int ioctl_sync_bh_stub(struct buffer_head *bh)
{
	struct kunit *test = active_ioctl_test;
	struct ioctl_test_ctx *ctx;

	if (!test)
		return 0;
	ctx = test->priv;
	if (!ctx)
		return 0;

	ctx->sync_bh_calls++;
	return ctx->sync_bh_err;
}

static int ioctl_get_inode_loc_stub(struct inode *inode, struct ext4_iloc *iloc)
{
	struct kunit *test = active_ioctl_test;
	struct ioctl_test_ctx *ctx;

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

static int ioctl_mark_iloc_dirty_stub(handle_t *handle, struct inode *inode,
				      struct ext4_iloc *iloc)
{
	brelse(iloc->bh);
	iloc->bh = NULL;
	return 0;
}

static void ioctl_init_mock_bh(struct buffer_head *bh,
			       struct block_device *bdev, struct folio *folio,
			       u8 *data, size_t size, sector_t blocknr)
{
	memset(bh, 0, sizeof(*bh));
	bh->b_bdev = bdev;
	bh->b_folio = folio;
	bh->b_data = (char *)data;
	bh->b_size = size;
	bh->b_blocknr = blocknr;
	atomic_set(&bh->b_count, 2);
	set_buffer_uptodate(bh);
}

static void ioctl_sync_backups_from_primary(struct ioctl_test_ctx *ctx)
{
	struct super_block *sb = ctx->sb;
	ext4_group_t g;

	ext4_superblock_csum_set(sb);
	for (g = 0; g < IOCTL_TEST_NGROUPS; g++) {
		struct ext4_super_block *bes;

		bes = (struct ext4_super_block *)ctx->backup_sb_bufs[g];
		memcpy(bes, ctx->es, sizeof(*bes));
		bes->s_block_group_nr = cpu_to_le16(g);
		if (ext4_has_feature_metadata_csum(sb))
			bes->s_checksum = ext4_superblock_csum(bes);
	}
}

static void ioctl_test_cleanup(struct ioctl_test_ctx *ctx)
{
	active_ioctl_test = NULL;

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);

	if (ctx->sb) {
		deactivate_super(ctx->sb);
		rcu_barrier();
	}

	if (ctx->es_shrinker_inited) {
		ext4_es_unregister_shrinker(&ctx->sbi);
		ext4_es_destroy_stats(&ctx->sbi);
	}

	percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);
	percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
	percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);

	if (ctx->bh_folio)
		folio_put(ctx->bh_folio);
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(ioctl_test_cleanup_action, ioctl_test_cleanup,
			    struct ioctl_test_ctx *);

static int ioctl_test_init(struct kunit *test)
{
	struct ioctl_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	ext4_group_t g;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	if (!ctx->bdev.bd_stats) {
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bd_stats");
	}

	ctx->bh_folio = folio_alloc(GFP_KERNEL, 0);
	if (!ctx->bh_folio) {
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bh_folio");
	}

	fc = fs_context_for_mount(&ioctl_test_fs_type, 0);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		folio_put(ctx->bh_folio);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	sb = sget_fc(fc, NULL, set_anon_super_fc);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		folio_put(ctx->bh_folio);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	ctx->sb = sb;
	sbi = &ctx->sbi;

	INIT_WORK(&sbi->s_sb_upd_work, ioctl_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, ioctl_test_err_timer, 0);

	err = percpu_counter_init(&sbi->s_freeclusters_counter, 0, GFP_KERNEL);
	if (err) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	if (err) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	err = percpu_counter_init(&sbi->s_freeinodes_counter, 0, GFP_KERNEL);
	if (err) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}

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
	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &ioctl_test_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = IOCTL_TEST_BLK_SIZE;
	sb->s_blocksize_bits = IOCTL_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_ioctl_kunit", sizeof(sb->s_id));

	err = ext4_es_register_shrinker(sbi);
	if (err) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	ctx->es_shrinker_inited = true;

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = IOCTL_TEST_BLK_BITS;
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

	ctx->es = (struct ext4_super_block *)ctx->primary_sb_buf;
	ctx->es->s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es->s_first_ino = cpu_to_le32(IOCTL_TEST_FIRST_INO);
	ctx->es->s_inode_size = cpu_to_le16(IOCTL_TEST_INODE_SIZE);
	ctx->es->s_want_extra_isize = cpu_to_le16(IOCTL_TEST_EXTRA_ISIZE);
	ctx->es->s_min_extra_isize = cpu_to_le16(IOCTL_TEST_EXTRA_ISIZE);
	ctx->es->s_first_data_block = cpu_to_le32(IOCTL_TEST_FIRST_DATA_BLK);
	ctx->es->s_blocks_per_group = cpu_to_le32(IOCTL_TEST_BLOCKS_PER_GRP);
	ctx->es->s_clusters_per_group = cpu_to_le32(IOCTL_TEST_BLOCKS_PER_GRP);
	ctx->es->s_inodes_per_group = cpu_to_le32(IOCTL_TEST_INODES_PER_GRP);
	ctx->es->s_inodes_count = cpu_to_le32(IOCTL_TEST_TOTAL_INODES);
	ext4_blocks_count_set(ctx->es, IOCTL_TEST_TOTAL_BLKS);
	ctx->es->s_checksum_type = EXT4_CRC32C_CHKSUM;
	ctx->es->s_feature_incompat = cpu_to_le32(EXT4_FEATURE_INCOMPAT_EXTENTS |
						  EXT4_FEATURE_INCOMPAT_64BIT);
	ctx->es->s_feature_ro_compat =
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER |
			    EXT4_FEATURE_RO_COMPAT_METADATA_CSUM |
			    EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE);
	generate_random_uuid(ctx->es->s_uuid);

	ioctl_init_mock_bh(&ctx->sb_bh, &ctx->bdev, ctx->bh_folio,
			   ctx->primary_sb_buf, IOCTL_TEST_BLK_SIZE,
			   IOCTL_TEST_FIRST_DATA_BLK);
	for (g = 0; g < IOCTL_TEST_NGROUPS; g++) {
		ext4_fsblk_t blk = IOCTL_TEST_FIRST_DATA_BLK +
				   (ext4_fsblk_t)g * IOCTL_TEST_BLOCKS_PER_GRP;

		ioctl_init_mock_bh(&ctx->backup_bhs[g], &ctx->bdev,
				   ctx->bh_folio, ctx->backup_sb_bufs[g],
				   IOCTL_TEST_BLK_SIZE, blk);
	}
	ioctl_init_mock_bh(&ctx->iloc_bh, &ctx->bdev, ctx->bh_folio,
			   ctx->iloc_buf, IOCTL_TEST_BLK_SIZE, 20);

	sbi->s_es = ctx->es;
	sbi->s_sbh = &ctx->sb_bh;
	sbi->s_blocks_per_group = IOCTL_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = IOCTL_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_inodes_per_group = IOCTL_TEST_INODES_PER_GRP;
	sbi->s_groups_count = IOCTL_TEST_NGROUPS;
	sbi->s_inode_size = IOCTL_TEST_INODE_SIZE;
	sbi->s_first_ino = IOCTL_TEST_FIRST_INO;
	sbi->s_want_extra_isize = IOCTL_TEST_EXTRA_ISIZE;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_csum_seed = ext4_chksum(~0, ctx->es->s_uuid,
				       sizeof(ctx->es->s_uuid));

	ioctl_sync_backups_from_primary(ctx);

	root_inode = new_inode(sb);
	if (!root_inode) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc root_inode");
	}
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_flags |= S_NOQUOTA;
	root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		up_write(&sb->s_umount);
		ioctl_test_cleanup(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc s_root");
	}

	up_write(&sb->s_umount);

	err = kunit_add_action_or_reset(test, ioctl_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	active_ioctl_test = test;

	kunit_activate_static_stub(test, ext4_sb_bread, ioctl_sb_bread_stub);
	kunit_activate_static_stub(test, ext4_ioctl_sync_bh,
				   ioctl_sync_bh_stub);
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   ioctl_get_inode_loc_stub);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   ioctl_mark_iloc_dirty_stub);

	return 0;
}

/*
 * Verify ext4_sb_setlabel, ext4_sb_setuuid, and ext4_sb_setparams across all
 * superblock tuneable fields, split 64-bit / 32-bit fields, feature masks,
 * and FORCE_FSCK state marking.
 */
static void test_sb_callbacks_and_setparams(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = ctx->es;
	struct ext4_tune_sb_params params;
	char label[EXT4_LABEL_MAX] = "ext4_kunit_lbl";
	u8 uuid[UUID_SIZE];
	int i;

	ext4_sb_setlabel_test(sbi, es, label);
	KUNIT_EXPECT_MEMEQ(test, es->s_volume_name, label, EXT4_LABEL_MAX);

	for (i = 0; i < UUID_SIZE; i++)
		uuid[i] = 0xa0 + i;
	ext4_sb_setuuid_test(sbi, es, uuid);
	KUNIT_EXPECT_MEMEQ(test, es->s_uuid, uuid, UUID_SIZE);

	memset(&params, 0, sizeof(params));
	sbi->s_def_hash_version = DX_HASH_TEA;
	sbi->s_csum_seed = 0xdeadbeefU;
	params.set_flags = EXT4_TUNE_FL_ERRORS_BEHAVIOR |
			   EXT4_TUNE_FL_MNT_COUNT |
			   EXT4_TUNE_FL_MAX_MNT_COUNT |
			   EXT4_TUNE_FL_CHECKINTRVAL |
			   EXT4_TUNE_FL_LAST_CHECK_TIME |
			   EXT4_TUNE_FL_RESERVED_BLOCKS |
			   EXT4_TUNE_FL_RESERVED_UID |
			   EXT4_TUNE_FL_RESERVED_GID |
			   EXT4_TUNE_FL_DEFAULT_MNT_OPTS |
			   EXT4_TUNE_FL_DEF_HASH_ALG |
			   EXT4_TUNE_FL_RAID_STRIDE |
			   EXT4_TUNE_FL_RAID_STRIPE_WIDTH |
			   EXT4_TUNE_FL_ENCODING |
			   EXT4_TUNE_FL_ENCODING_FLAGS |
			   EXT4_TUNE_FL_MOUNT_OPTS |
			   EXT4_TUNE_FL_EDIT_FEATURES |
			   EXT4_TUNE_FL_FORCE_FSCK;
	params.errors_behavior = EXT4_ERRORS_RO;
	params.mnt_count = 7;
	params.max_mnt_count = 42;
	params.checkinterval = 86400;
	params.last_check_time = 1700000000ULL;
	params.reserved_blocks = 0x100000055ULL;
	params.reserved_uid = 0x12345678U;
	params.reserved_gid = 0x87654321U;
	params.default_mnt_opts = EXT4_DEFM_ACL | EXT4_DEFM_XATTR_USER;
	params.def_hash_alg = DX_HASH_HALF_MD4;
	params.raid_stride = 16;
	params.raid_stripe_width = 128;
	params.encoding = EXT4_ENC_UTF8_12_1;
	params.encoding_flags = SB_ENC_STRICT_MODE_FL;
	strscpy(params.mount_opts, "nodelalloc,noatime",
		sizeof(params.mount_opts));
	params.set_feature_compat_mask = EXT4_FEATURE_COMPAT_DIR_INDEX;
	params.set_feature_incompat_mask = EXT4_FEATURE_INCOMPAT_CSUM_SEED;
	params.set_feature_ro_compat_mask = EXT4_FEATURE_RO_COMPAT_PROJECT;

	ext4_sb_setparams_test(sbi, es, &params);

	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_errors), EXT4_ERRORS_RO);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_mnt_count), 7);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_max_mnt_count), 42);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_checkinterval), 86400U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_lastcheck), 1700000000U);
	KUNIT_EXPECT_EQ(test, ext4_r_blocks_count(es), 0x100000055ULL);
	KUNIT_EXPECT_EQ(test, ext4_get_resuid(es), 0x12345678U);
	KUNIT_EXPECT_EQ(test, ext4_get_resgid(es), 0x87654321U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_default_mount_opts),
			(u32)(EXT4_DEFM_ACL | EXT4_DEFM_XATTR_USER));
	/* EDIT_FEATURES + DIR_INDEX overrides s_def_hash_version from sbi */
	KUNIT_EXPECT_EQ(test, es->s_def_hash_version, DX_HASH_TEA);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_raid_stride), 16);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_raid_stripe_width), 128U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_encoding), EXT4_ENC_UTF8_12_1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(es->s_encoding_flags),
			SB_ENC_STRICT_MODE_FL);
	KUNIT_EXPECT_STREQ(test, (char *)es->s_mount_opts,
			   "nodelalloc,noatime");
	KUNIT_EXPECT_NE(test,
			le32_to_cpu(es->s_feature_compat) &
			EXT4_FEATURE_COMPAT_DIR_INDEX, 0U);
	KUNIT_EXPECT_NE(test,
			le32_to_cpu(es->s_feature_incompat) &
			EXT4_FEATURE_INCOMPAT_CSUM_SEED, 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(es->s_checksum_seed), 0xdeadbeefU);
	KUNIT_EXPECT_NE(test,
			le32_to_cpu(es->s_feature_ro_compat) &
			EXT4_FEATURE_RO_COMPAT_PROJECT, 0U);
	KUNIT_EXPECT_NE(test, le16_to_cpu(es->s_state) & EXT4_ERROR_FS, 0);
}

/*
 * Verify ext4_validate_tune_sb_params rejects malformed mount_opts, invalid
 * flags, out-of-range parameters, unsupported feature edits, and standalone
 * encoding updates, while normalizing DIR_INDEX and CASEFOLD enablement.
 */
static void test_validate_tune_sb_params(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = ctx->es;
	struct ext4_tune_sb_params params;
	uuid_t uu;

	/* Unterminated mount_opts -> -E2BIG */
	memset(&params, 0, sizeof(params));
	memset(params.mount_opts, 'a', sizeof(params.mount_opts));
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-E2BIG);

	/* Unknown set_flags bit -> -EOPNOTSUPP */
	memset(&params, 0, sizeof(params));
	params.set_flags = 0x80000000U;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EOPNOTSUPP);

	/* Invalid errors_behavior -> -EINVAL */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_ERRORS_BEHAVIOR;
	params.errors_behavior = EXT4_ERRORS_PANIC + 1;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);

	/* Reserved blocks > blocks_count / 2 -> -EINVAL, <= half -> 0 */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_RESERVED_BLOCKS;
	params.reserved_blocks = ext4_blocks_count(es) / 2 + 1;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);
	params.reserved_blocks = ext4_blocks_count(es) / 2;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			0);

	/* Hash algorithm validation: SIPHASH and > DX_HASH_LAST rejected */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_DEF_HASH_ALG;
	params.def_hash_alg = DX_HASH_SIPHASH;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);
	params.def_hash_alg = DX_HASH_LAST + 1;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);
	params.def_hash_alg = DX_HASH_HALF_MD4;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			0);

	/* Mutual exclusion of FEATURES and EDIT_FEATURES */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_FEATURES | EXT4_TUNE_FL_EDIT_FEATURES;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);

	/* Clearing an existing feature via FEATURES -> -EOPNOTSUPP */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_FEATURES;
	params.feature_compat = le32_to_cpu(es->s_feature_compat);
	params.feature_incompat = 0;
	params.feature_ro_compat = le32_to_cpu(es->s_feature_ro_compat);
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EOPNOTSUPP);

	/* Enabling DIR_INDEX via FEATURES initializes hash seed & default alg */
	memset(sbi->s_hash_seed, 0, sizeof(sbi->s_hash_seed));
	sbi->s_def_hash_version = 0;
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_FEATURES;
	params.feature_compat = le32_to_cpu(es->s_feature_compat) |
				EXT4_FEATURE_COMPAT_DIR_INDEX;
	params.feature_incompat = le32_to_cpu(es->s_feature_incompat);
	params.feature_ro_compat = le32_to_cpu(es->s_feature_ro_compat) |
				   EXT4_FEATURE_RO_COMPAT_DIR_NLINK;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			0);
	KUNIT_EXPECT_NE(test, params.set_flags & EXT4_TUNE_FL_EDIT_FEATURES,
			0U);
	KUNIT_EXPECT_EQ(test, params.set_feature_compat_mask,
			(u32)EXT4_FEATURE_COMPAT_DIR_INDEX);
	KUNIT_EXPECT_EQ(test, params.set_feature_ro_compat_mask,
			(u32)EXT4_FEATURE_RO_COMPAT_DIR_NLINK);
	memcpy(&uu, sbi->s_hash_seed, UUID_SIZE);
	KUNIT_EXPECT_FALSE(test, uuid_is_null(&uu));
	KUNIT_EXPECT_EQ(test, sbi->s_def_hash_version, DX_HASH_HALF_MD4);

	/* Encoding without CASEFOLD -> -EINVAL; with CASEFOLD -> UTF8_12_1 */
	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_ENCODING;
	params.encoding = EXT4_ENC_UTF8_12_1;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			-EINVAL);

	memset(&params, 0, sizeof(params));
	params.set_flags = EXT4_TUNE_FL_EDIT_FEATURES;
	params.set_feature_incompat_mask = EXT4_FEATURE_INCOMPAT_CASEFOLD;
	KUNIT_EXPECT_EQ(test, ext4_validate_tune_sb_params_test(sb, &params),
			0);
	KUNIT_EXPECT_EQ(test, params.encoding, EXT4_ENC_UTF8_12_1);
	KUNIT_EXPECT_EQ(test, params.encoding_flags, 0);
}

/*
 * Verify ext4_update_backup_sb, ext4_update_superblocks_fn, and
 * ext4_update_overhead across primary + 4 sparse_super backup groups (1, 3,
 * 5, 7), covering both journaled (first 2 backups) and unjournaled (remaining
 * backups) update paths, EXT4_FLAGS_RESIZING contention, and overhead skip
 * conditions.
 */
static void test_update_superblocks_and_overhead(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	static const ext4_group_t backup_grps[] = { 1, 3, 5, 7 };
	char new_label[EXT4_LABEL_MAX] = "updated_label";
	int i;

	/* Group 2 has no backup superblock under sparse_super */
	KUNIT_EXPECT_EQ(test,
			ext4_update_backup_sb_test(sb, NULL, 2,
						   ext4_sb_setlabel_test,
						   new_label),
			0);

	/* Contention guard when EXT4_FLAGS_RESIZING is already set */
	set_bit(EXT4_FLAGS_RESIZING, &sbi->s_ext4_flags);
	KUNIT_EXPECT_EQ(test,
			ext4_update_superblocks_fn_test(sb,
							ext4_sb_setlabel_test,
							new_label),
			-EBUSY);
	clear_bit(EXT4_FLAGS_RESIZING, &sbi->s_ext4_flags);

	/* Full superblock update across primary (grp 0) + 4 backups */
	ctx->sync_bh_calls = 0;
	KUNIT_EXPECT_EQ(test,
			ext4_update_superblocks_fn_test(sb,
							ext4_sb_setlabel_test,
							new_label),
			0);
	KUNIT_EXPECT_FALSE(test,
			   test_bit(EXT4_FLAGS_RESIZING, &sbi->s_ext4_flags));
	/* 1 primary + 4 backup superblocks synced */
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 5);
	KUNIT_EXPECT_MEMEQ(test, ctx->es->s_volume_name, new_label,
			   EXT4_LABEL_MAX);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es->s_checksum),
			le32_to_cpu(ext4_superblock_csum(ctx->es)));

	for (i = 0; i < ARRAY_SIZE(backup_grps); i++) {
		ext4_group_t g = backup_grps[i];
		struct ext4_super_block *bes;

		bes = (struct ext4_super_block *)ctx->backup_sb_bufs[g];
		KUNIT_EXPECT_MEMEQ(test, bes->s_volume_name, new_label,
				   EXT4_LABEL_MAX);
		KUNIT_EXPECT_EQ(test, le32_to_cpu(bes->s_checksum),
				le32_to_cpu(ext4_superblock_csum(bes)));
	}

	/* ext4_update_overhead skip & force behavior */
	ctx->sync_bh_calls = 0;
	sbi->s_overhead = 0;
	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, false), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 0);

	sbi->s_overhead = 77;
	sb->s_flags |= SB_RDONLY;
	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, true), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 0);
	sb->s_flags &= ~SB_RDONLY;

	set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, true), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 0);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);

	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, false), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 5);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es->s_overhead_clusters), 77U);

	/* Matching s_overhead skips when !force, updates when force == true */
	ctx->sync_bh_calls = 0;
	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, false), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 0);
	KUNIT_EXPECT_EQ(test, ext4_update_overhead(sb, true), 0);
	KUNIT_EXPECT_EQ(test, ctx->sync_bh_calls, 5);
}

/*
 * Verify swap_inode_data swaps i_data, i_disksize, i_size, i_version,
 * atime/mtime, and EXT4_FL_SHOULD_SWAP flags while preserving non-swapped
 * flags, clears both extent-status caches, and restores exact state when
 * invoked a second time. Also verify ext4_reset_inode_seed.
 */
static void test_swap_inode_data_and_seed(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode1, *inode2;
	struct ext4_inode_info *ei1, *ei2;
	struct timespec64 at1 = { .tv_sec = 1000, .tv_nsec = 111 };
	struct timespec64 at2 = { .tv_sec = 2000, .tv_nsec = 222 };
	struct timespec64 mt1 = { .tv_sec = 3000, .tv_nsec = 333 };
	struct timespec64 mt2 = { .tv_sec = 4000, .tv_nsec = 444 };
	__le32 orig_data1[EXT4_N_BLOCKS];
	__le32 orig_data2[EXT4_N_BLOCKS];
	u32 seed1, seed2;
	int i;

	inode1 = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, inode1);
	inode2 = new_inode(sb);
	if (!inode2) {
		iput(inode1);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc inode2");
	}
	ei1 = EXT4_I(inode1);
	ei2 = EXT4_I(inode2);

	for (i = 0; i < EXT4_N_BLOCKS; i++) {
		ei1->i_data[i] = cpu_to_le32(0x100 + i);
		ei2->i_data[i] = cpu_to_le32(0x200 + i);
	}
	memcpy(orig_data1, ei1->i_data, sizeof(orig_data1));
	memcpy(orig_data2, ei2->i_data, sizeof(orig_data2));

	ei1->i_flags = EXT4_EXTENTS_FL | EXT4_HUGE_FILE_FL | EXT4_IMMUTABLE_FL;
	ei2->i_flags = EXT4_NOATIME_FL;
	ei1->i_disksize = 0x11000;
	ei2->i_disksize = 0x22000;
	i_size_write(inode1, 0x10500);
	i_size_write(inode2, 0x20500);
	inode_set_iversion_raw(inode1, 10);
	inode_set_iversion_raw(inode2, 20);
	inode_set_atime_to_ts(inode1, at1);
	inode_set_atime_to_ts(inode2, at2);
	inode_set_mtime_to_ts(inode1, mt1);
	inode_set_mtime_to_ts(inode2, mt2);

	ext4_es_insert_extent(inode1, 0, 4, 50, EXTENT_STATUS_WRITTEN, false);
	ext4_es_insert_extent(inode2, 8, 2, 60, EXTENT_STATUS_WRITTEN, false);
	KUNIT_EXPECT_EQ(test, ei1->i_es_all_nr, 1U);
	KUNIT_EXPECT_EQ(test, ei2->i_es_all_nr, 1U);

	ext4_swap_inode_data_test(inode1, inode2);

	KUNIT_EXPECT_MEMEQ(test, ei1->i_data, orig_data2, sizeof(orig_data2));
	KUNIT_EXPECT_MEMEQ(test, ei2->i_data, orig_data1, sizeof(orig_data1));
	KUNIT_EXPECT_EQ(test, ei1->i_flags, (u32)EXT4_IMMUTABLE_FL);
	KUNIT_EXPECT_EQ(test, ei2->i_flags,
			(u32)(EXT4_NOATIME_FL | EXT4_EXTENTS_FL |
			      EXT4_HUGE_FILE_FL));
	KUNIT_EXPECT_EQ(test, ei1->i_disksize, (loff_t)0x22000);
	KUNIT_EXPECT_EQ(test, ei2->i_disksize, (loff_t)0x11000);
	KUNIT_EXPECT_EQ(test, i_size_read(inode1), (loff_t)0x20500);
	KUNIT_EXPECT_EQ(test, i_size_read(inode2), (loff_t)0x10500);
	KUNIT_EXPECT_EQ(test, inode_peek_iversion_raw(inode1), 20ULL);
	KUNIT_EXPECT_EQ(test, inode_peek_iversion_raw(inode2), 10ULL);
	KUNIT_EXPECT_EQ(test, inode_get_atime_sec(inode1), 2000LL);
	KUNIT_EXPECT_EQ(test, inode_get_mtime_sec(inode1), 4000LL);
	KUNIT_EXPECT_EQ(test, ei1->i_es_all_nr, 0U);
	KUNIT_EXPECT_EQ(test, ei2->i_es_all_nr, 0U);

	/* Second swap restores exact pre-swap attributes */
	ext4_swap_inode_data_test(inode1, inode2);
	KUNIT_EXPECT_MEMEQ(test, ei1->i_data, orig_data1, sizeof(orig_data1));
	KUNIT_EXPECT_MEMEQ(test, ei2->i_data, orig_data2, sizeof(orig_data2));
	KUNIT_EXPECT_EQ(test, ei1->i_flags,
			(u32)(EXT4_EXTENTS_FL | EXT4_HUGE_FILE_FL |
			      EXT4_IMMUTABLE_FL));
	KUNIT_EXPECT_EQ(test, ei2->i_flags, (u32)EXT4_NOATIME_FL);
	KUNIT_EXPECT_EQ(test, ei1->i_disksize, (loff_t)0x11000);
	KUNIT_EXPECT_EQ(test, i_size_read(inode1), (loff_t)0x10500);

	/* ext4_reset_inode_seed with and without metadata_csum */
	inode1->i_ino = 42;
	inode1->i_generation = 7;
	ei1->i_csum_seed = 0;
	ext4_reset_inode_seed(inode1);
	seed1 = ei1->i_csum_seed;
	KUNIT_EXPECT_NE(test, seed1, 0U);

	inode1->i_generation = 8;
	ext4_reset_inode_seed(inode1);
	seed2 = ei1->i_csum_seed;
	KUNIT_EXPECT_NE(test, seed2, seed1);

	ext4_clear_feature_metadata_csum(sb);
	ei1->i_csum_seed = 0x12345678U;
	ext4_reset_inode_seed(inode1);
	KUNIT_EXPECT_EQ(test, ei1->i_csum_seed, 0x12345678U);

	iput(inode1);
	iput(inode2);
}

/*
 * Verify ext4_ioctl_check_immutable, dax_compatible, ext4_fileattr_get, and
 * ext4_fileattr_set flag masking, mode checks, quota-file protection, and
 * end-to-end VFS inode flag synchronization.
 */
static void test_immutable_dax_and_fileattr(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *reg_inode, *dir_inode;
	struct dentry *reg_dentry, *dir_dentry;
	struct ext4_inode_info *rei, *dei;
	struct file_kattr fa;

	reg_inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, reg_inode);
	dir_inode = new_inode(sb);
	if (!dir_inode) {
		iput(reg_inode);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc dir_inode");
	}
	reg_inode->i_ino = 100;
	reg_inode->i_mode = S_IFREG | 0644;
	dir_inode->i_ino = 101;
	dir_inode->i_mode = S_IFDIR | 0755;
	rei = EXT4_I(reg_inode);
	dei = EXT4_I(dir_inode);

	reg_dentry = d_alloc_anon(sb);
	if (!reg_dentry) {
		iput(dir_inode);
		iput(reg_inode);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc reg_dentry");
	}
	d_instantiate(reg_dentry, reg_inode);
	dir_dentry = d_alloc_anon(sb);
	if (!dir_dentry) {
		dput(reg_dentry);
		iput(dir_inode);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc dir_dentry");
	}
	d_instantiate(dir_dentry, dir_inode);

	/* ext4_ioctl_check_immutable */
	ext4_set_feature_project(sb);
	rei->i_projid = make_kprojid(&init_user_ns, 10);
	rei->i_flags = EXT4_IMMUTABLE_FL | EXT4_NOATIME_FL;
	KUNIT_EXPECT_EQ(test,
			ext4_ioctl_check_immutable_test(reg_inode, 10,
					EXT4_IMMUTABLE_FL | EXT4_NOATIME_FL),
			0);
	/* Clearing IMMUTABLE_FL is allowed here (CAP_LINUX_IMMUTABLE checked in VFS) */
	KUNIT_EXPECT_EQ(test,
			ext4_ioctl_check_immutable_test(reg_inode, 10,
							EXT4_NOATIME_FL),
			0);
	/* Changing other flags or projid while keeping IMMUTABLE_FL -> -EPERM */
	KUNIT_EXPECT_EQ(test,
			ext4_ioctl_check_immutable_test(reg_inode, 10,
							EXT4_IMMUTABLE_FL),
			-EPERM);
	KUNIT_EXPECT_EQ(test,
			ext4_ioctl_check_immutable_test(reg_inode, 11,
					EXT4_IMMUTABLE_FL | EXT4_NOATIME_FL),
			-EPERM);

	/* dax_compatible */
	KUNIT_EXPECT_FALSE(test,
			   ext4_dax_compatible_test(reg_inode, EXT4_VERITY_FL,
						    EXT4_DAX_FL));
	KUNIT_EXPECT_FALSE(test,
			   ext4_dax_compatible_test(reg_inode,
						    EXT4_INLINE_DATA_FL,
						    EXT4_DAX_FL));
	KUNIT_EXPECT_FALSE(test,
			   ext4_dax_compatible_test(reg_inode, EXT4_DAX_FL,
						    EXT4_JOURNAL_DATA_FL));
	/* Inline directory MAY toggle DAX_FL */
	KUNIT_EXPECT_TRUE(test,
			  ext4_dax_compatible_test(dir_inode,
						   EXT4_INLINE_DATA_FL,
						   EXT4_INLINE_DATA_FL |
						   EXT4_DAX_FL));
	/* VERITY_IN_PROGRESS blocks enabling DAX_FL */
	ext4_set_inode_state(reg_inode, EXT4_STATE_VERITY_IN_PROGRESS);
	KUNIT_EXPECT_FALSE(test,
			   ext4_dax_compatible_test(reg_inode, 0, EXT4_DAX_FL));
	ext4_clear_inode_state(reg_inode, EXT4_STATE_VERITY_IN_PROGRESS);

	/* ext4_fileattr_get: strips PROJINHERIT on reg file, keeps on dir */
	rei->i_flags = EXT4_NOATIME_FL | EXT4_PROJINHERIT_FL | EXT4_RESERVED_FL;
	dei->i_flags = EXT4_NOATIME_FL | EXT4_PROJINHERIT_FL;
	dei->i_projid = make_kprojid(&init_user_ns, 25);

	memset(&fa, 0, sizeof(fa));
	KUNIT_EXPECT_EQ(test, ext4_fileattr_get(reg_dentry, &fa), 0);
	KUNIT_EXPECT_EQ(test, fa.flags, (u32)EXT4_NOATIME_FL);
	KUNIT_EXPECT_EQ(test, fa.fsx_projid, 10U);

	memset(&fa, 0, sizeof(fa));
	KUNIT_EXPECT_EQ(test, ext4_fileattr_get(dir_dentry, &fa), 0);
	KUNIT_EXPECT_EQ(test, fa.flags,
			(u32)(EXT4_NOATIME_FL | EXT4_PROJINHERIT_FL));
	KUNIT_EXPECT_EQ(test, fa.fsx_projid, 25U);

	/* ext4_fileattr_set error paths and success path */
	ext4_clear_feature_project(sb);
	rei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	rei->i_flags = 0;

	/* Flag outside EXT4_FL_USER_VISIBLE -> -EOPNOTSUPP */
	memset(&fa, 0, sizeof(fa));
	fileattr_fill_flags(&fa, EXT4_RESERVED_FL);
	KUNIT_EXPECT_EQ(test,
			ext4_fileattr_set(&nop_mnt_idmap, reg_dentry, &fa),
			-EOPNOTSUPP);

	/* Directory-only flag on regular file -> -EOPNOTSUPP */
	memset(&fa, 0, sizeof(fa));
	fileattr_fill_flags(&fa, EXT4_PROJINHERIT_FL);
	KUNIT_EXPECT_EQ(test,
			ext4_fileattr_set(&nop_mnt_idmap, reg_dentry, &fa),
			-EOPNOTSUPP);

	/* Quota file cannot be modified -> -EPERM */
	reg_inode->i_flags |= S_NOQUOTA;
	memset(&fa, 0, sizeof(fa));
	fileattr_fill_flags(&fa, EXT4_NOATIME_FL);
	KUNIT_EXPECT_EQ(test,
			ext4_fileattr_set(&nop_mnt_idmap, reg_dentry, &fa),
			-EPERM);
	reg_inode->i_flags &= ~S_NOQUOTA;

	/* CASEFOLD_FL without casefold feature -> -EOPNOTSUPP */
	memset(&fa, 0, sizeof(fa));
	fileattr_fill_flags(&fa, EXT4_CASEFOLD_FL);
	KUNIT_EXPECT_EQ(test,
			ext4_fileattr_set(&nop_mnt_idmap, dir_dentry, &fa),
			-EOPNOTSUPP);

	/* End-to-end setflags on regular file updates ei->i_flags & VFS flags */
	memset(&fa, 0, sizeof(fa));
	fileattr_fill_flags(&fa, EXT4_NOATIME_FL | EXT4_SYNC_FL |
				 EXT4_NODUMP_FL);
	KUNIT_EXPECT_EQ(test,
			ext4_fileattr_set(&nop_mnt_idmap, reg_dentry, &fa),
			0);
	KUNIT_EXPECT_EQ(test,
			rei->i_flags & (EXT4_NOATIME_FL | EXT4_SYNC_FL |
					EXT4_NODUMP_FL),
			(u32)(EXT4_NOATIME_FL | EXT4_SYNC_FL | EXT4_NODUMP_FL));
	KUNIT_EXPECT_NE(test, reg_inode->i_flags & S_NOATIME, 0U);
	KUNIT_EXPECT_NE(test, reg_inode->i_flags & S_SYNC, 0U);

	dput(reg_dentry);
	dput(dir_dentry);
}

/*
 * Verify ext4_force_shutdown state transitions and ext4_ioctl /
 * ext4_compat_ioctl command dispatch for unknown commands, feature-gated
 * commands, mode checks, FITRIM preconditions, and EXT4_IOC_CLEAR_ES_CACHE.
 */
static void test_force_shutdown_and_ioctl_dispatch(struct kunit *test)
{
	struct ioctl_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode;
	struct dentry *dentry;
	struct vfsmount mnt = {
		.mnt_root	= sb->s_root,
		.mnt_sb		= sb,
		.mnt_idmap	= &nop_mnt_idmap,
	};

	inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);
	inode->i_ino = 200;
	inode->i_mode = S_IFREG | 0644;
	dentry = d_alloc_anon(sb);
	if (!dentry) {
		iput(inode);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc dentry");
	}
	d_instantiate(dentry, inode);

	{
		struct file filp = {
			.f_inode	= inode,
			.f_path		= {
				.mnt	= &mnt,
				.dentry	= dentry,
			},
			.f_mode		= FMODE_READ | FMODE_WRITE,
		};

		/* Unknown command dispatch */
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, 0xffffffffU, 0),
				-ENOTTY);
#ifdef CONFIG_COMPAT
		KUNIT_EXPECT_EQ(test, ext4_compat_ioctl(&filp, 0xffffffffU, 0),
				-ENOIOCTLCMD);
#endif

		/* Encryption and verity ioctls return -EOPNOTSUPP when off */
		KUNIT_EXPECT_EQ(test,
				ext4_ioctl(&filp, FS_IOC_SET_ENCRYPTION_POLICY,
					   0),
				-EOPNOTSUPP);
		KUNIT_EXPECT_EQ(test,
				ext4_ioctl(&filp, FS_IOC_GET_ENCRYPTION_POLICY,
					   0),
				-EOPNOTSUPP);
		KUNIT_EXPECT_EQ(test,
				ext4_ioctl(&filp, FS_IOC_ENABLE_VERITY, 0),
				-EOPNOTSUPP);
		KUNIT_EXPECT_EQ(test,
				ext4_ioctl(&filp, FS_IOC_MEASURE_VERITY, 0),
				-EOPNOTSUPP);
#ifdef CONFIG_COMPAT
		KUNIT_EXPECT_EQ(test,
				ext4_compat_ioctl(&filp, FS_IOC_ENABLE_VERITY,
						  0),
				-EOPNOTSUPP);
#endif

		/* SETVERSION rejected with -ENOTTY when metadata_csum is on */
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, EXT4_IOC_SETVERSION, 0),
				-ENOTTY);

		/* FITRIM: no discard -> -EOPNOTSUPP; NOLOAD+journal -> -EROFS */
		ctx->bdev_queue.limits.max_discard_sectors = 0;
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, FITRIM, 0),
				-EOPNOTSUPP);

		ctx->bdev_queue.limits.max_discard_sectors = 1024;
		set_opt(sb, NOLOAD);
		ext4_set_feature_journal(sb);
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, FITRIM, 0), -EROFS);
		clear_opt(sb, NOLOAD);
		ext4_clear_feature_journal(sb);

		/* MOVE_EXT and SWAP_BOOT require FMODE_WRITE -> -EBADF on RO */
		filp.f_mode = FMODE_READ;
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, EXT4_IOC_MOVE_EXT, 0),
				-EBADF);
		KUNIT_EXPECT_EQ(test, ext4_ioctl(&filp, EXT4_IOC_SWAP_BOOT, 0),
				-EBADF);
		filp.f_mode = FMODE_READ | FMODE_WRITE;

		/* EXT4_IOC_CLEAR_ES_CACHE empties inode extent status tree */
		ext4_es_insert_extent(inode, 0, 8, 120, EXTENT_STATUS_WRITTEN,
				      false);
		KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_es_all_nr, 1U);
		KUNIT_EXPECT_EQ(test,
				ext4_ioctl(&filp, EXT4_IOC_CLEAR_ES_CACHE, 0),
				0);
		KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_es_all_nr, 0U);
	}

	/* ext4_force_shutdown validation, NOLOGFLUSH state, and idempotency */
	KUNIT_EXPECT_EQ(test,
			ext4_force_shutdown(sb, EXT4_GOING_FLAGS_NOLOGFLUSH + 1),
			-EINVAL);
	set_opt(sb, DISCARD);
	KUNIT_EXPECT_EQ(test,
			ext4_force_shutdown(sb, EXT4_GOING_FLAGS_NOLOGFLUSH),
			0);
	KUNIT_EXPECT_TRUE(test, ext4_forced_shutdown(sb));
	KUNIT_EXPECT_EQ(test, test_opt(sb, DISCARD), 0U);
	/* Repeat shutdown call is a no-op returning 0 */
	KUNIT_EXPECT_EQ(test,
			ext4_force_shutdown(sb, EXT4_GOING_FLAGS_DEFAULT),
			0);

	dput(dentry);
}

static struct kunit_case ioctl_test_cases[] = {
	KUNIT_CASE(test_sb_callbacks_and_setparams),
	KUNIT_CASE(test_validate_tune_sb_params),
	KUNIT_CASE(test_update_superblocks_and_overhead),
	KUNIT_CASE(test_swap_inode_data_and_seed),
	KUNIT_CASE(test_immutable_dax_and_fileattr),
	KUNIT_CASE(test_force_shutdown_and_ioctl_dispatch),
	{}
};

static struct kunit_suite ioctl_test_suite = {
	.name = "ext4_ioctl_test",
	.init = ioctl_test_init,
	.test_cases = ioctl_test_cases,
};

kunit_test_suites(&ioctl_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 ioctl operations");
MODULE_LICENSE("GPL");
