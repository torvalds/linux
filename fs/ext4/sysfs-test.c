// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/sysfs.c
 */

#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"

#define SYSFS_TEST_BLK_SIZE		4096U
#define SYSFS_TEST_BLK_BITS		12U
#define SYSFS_TEST_TOTAL_BLKS		4096U
#define SYSFS_TEST_CLUSTERS_PER_GRP	256U

struct sysfs_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct inode buddy_inode;
	struct buffer_head sb_bh;
	journal_t mock_journal;
	bool counters_inited;
};

static struct inode *sysfs_test_alloc_inode(struct super_block *sb)
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
	ei->i_extra_isize = 32;
	ei->i_last_alloc_group = ~0U;

	return &ei->vfs_inode;
}

static void sysfs_test_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

static void sysfs_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **sysfs_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations sysfs_test_sops = {
	.alloc_inode	= sysfs_test_alloc_inode,
	.free_inode	= sysfs_test_free_inode,
	.evict_inode	= sysfs_test_evict_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= sysfs_test_get_dquots,
#endif
};

static void sysfs_test_sb_upd_work(struct work_struct *work)
{
}

static void sysfs_test_err_timer(struct timer_list *t)
{
}

static int sysfs_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type sysfs_test_fs_type = {
	.name		 = "ext4_sysfs_kunit",
	.init_fs_context = sysfs_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void sysfs_test_cleanup(struct sysfs_test_ctx *ctx)
{
	ctx->sbi.s_journal = NULL;

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);

	deactivate_super(ctx->sb);
	rcu_barrier();

	if (ctx->counters_inited) {
		percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
		percpu_counter_destroy(&ctx->sbi.s_sra_exceeded_retry_limit);
	}
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(sysfs_test_cleanup_action, sysfs_test_cleanup,
			    struct sysfs_test_ctx *);

static int sysfs_test_init(struct kunit *test)
{
	struct sysfs_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	if (!ctx->bdev.bd_stats) {
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bd_stats");
	}

	fc = fs_context_for_mount(&sysfs_test_fs_type, 0);
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

	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	if (err)
		goto err_sb;
	err = percpu_counter_init(&sbi->s_sra_exceeded_retry_limit, 0,
				  GFP_KERNEL);
	if (err) {
		percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
		goto err_sb;
	}
	ctx->counters_inited = true;

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &sysfs_test_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = SYSFS_TEST_BLK_SIZE;
	sb->s_blocksize_bits = SYSFS_TEST_BLK_BITS;
	strscpy(sb->s_id, "ext4_sysfs_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = SYSFS_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_disk.part0 = &ctx->bdev;
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	inode_init_once(&ctx->buddy_inode);
	ctx->buddy_inode.i_sb = sb;
	sbi->s_buddy_cache = &ctx->buddy_inode;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ext4_blocks_count_set(&ctx->es, SYSFS_TEST_TOTAL_BLKS);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;
	sbi->s_clusters_per_group = SYSFS_TEST_CLUSTERS_PER_GRP;
	set_opt(sb, ERRORS_CONT);

	mutex_init(&sbi->s_error_notify_mutex);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	INIT_WORK(&sbi->s_sb_upd_work, sysfs_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, sysfs_test_err_timer, 0);

	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

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

	err = kunit_add_action_or_reset(test, sysfs_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	return 0;

err_counters:
	percpu_counter_destroy(&sbi->s_sra_exceeded_retry_limit);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
err_sb:
	deactivate_locked_super(sb);
	rcu_barrier();
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	return err;
}

static void test_generic_pointer_attrs(struct kunit *test)
{
	struct sysfs_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;
	char *buf;

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_show_test(sbi, "no_such_attr", buf),
			(ssize_t)-ENOENT);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "no_such_attr",
						   "1", 1),
			(ssize_t)-ENOENT);

	/* Explicit pointer RO attr: max_writeback_mb_bump == 128 */
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi,
				"max_writeback_mb_bump", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "128\n");

	/* RW sbi unsigned int (attr_pointer_ui) and unsigned long (ul) */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "inode_goal",
						   "  42\n", 5),
			(ssize_t)5);
	KUNIT_EXPECT_EQ(test, sbi->s_inode_goal, 42U);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "inode_goal", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "42\n");

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "last_trim_minblks",
						   "1048576", 7),
			(ssize_t)7);
	KUNIT_EXPECT_EQ(test, sbi->s_last_trim_minblks, 1048576UL);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "last_trim_minblks",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "1048576\n");

	/* RW positive int (attr_pointer_pi) rejects negative values */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_ratelimit_burst",
						   "-1", 2),
			(ssize_t)-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_ratelimit_burst",
						   "0x80000000", 10),
			(ssize_t)-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_ratelimit_burst",
						   "15", 2),
			(ssize_t)2);
	KUNIT_EXPECT_EQ(test, sbi->s_err_ratelimit_state.burst, 15);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "err_ratelimit_burst",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "15\n");

	/* RO atomic_t on sbi */
	atomic_set(&sbi->s_warning_count, 9);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "warning_count", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "9\n");

	/* RO superblock LE32, U8, LE64, and fixed-size string attrs */
	es->s_error_count = cpu_to_le32(77);
	es->s_first_error_errcode = EXT4_ERR_EIO;
	es->s_first_error_block = cpu_to_le64(0x123456789abcULL);
	memset(es->s_first_error_func, 'A', sizeof(es->s_first_error_func));

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "errors_count", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "77\n");

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "first_error_errcode",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "2\n");

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "first_error_block",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "20015998343868\n");

	/* Non-NUL-terminated 32-byte s_first_error_func is bounded to 32 */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_show_test(sbi, "first_error_func",
						  buf),
			(ssize_t)33);
	KUNIT_EXPECT_EQ(test, buf[32], '\n');
}

static void test_validated_rw_attrs(struct kunit *test)
{
	struct sysfs_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;
	char *buf;

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	/* inode_readahead_blks: 0 or power-of-2 <= 0x40000000 */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "inode_readahead_blks",
						   "0", 1),
			(ssize_t)1);
	KUNIT_EXPECT_EQ(test, sbi->s_inode_readahead_blks, 0U);

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "inode_readahead_blks",
						   "0x40000000", 10),
			(ssize_t)10);
	KUNIT_EXPECT_EQ(test, sbi->s_inode_readahead_blks, 0x40000000U);

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "inode_readahead_blks",
						   "3", 1),
			(ssize_t)-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "inode_readahead_blks",
						   "0x80000000", 10),
			(ssize_t)-EINVAL);

	/*
	 * reserved_clusters: 4096 blocks >> 2 cluster_bits == 1024 clusters;
	 * valid range is [0, 1023].
	 */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "reserved_clusters",
						   "1023", 4),
			(ssize_t)4);
	KUNIT_EXPECT_EQ(test, atomic64_read(&sbi->s_resv_clusters), 1023LL);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "reserved_clusters",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "1023\n");

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "reserved_clusters",
						   "1024", 4),
			(ssize_t)-EINVAL);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "reserved_clusters",
						   "0xffffffffffffffff", 18),
			(ssize_t)-EINVAL);

	/* mb_best_avail_max_trim_order: [0, 64] */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi,
				"mb_best_avail_max_trim_order", "64", 2),
			(ssize_t)2);
	KUNIT_EXPECT_EQ(test, sbi->s_mb_best_avail_max_trim_order, 64U);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi,
				"mb_best_avail_max_trim_order", "65", 2),
			(ssize_t)-EINVAL);

	/* mb_group_prealloc: [0, s_clusters_per_group == 256] */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "mb_group_prealloc",
						   "256", 3),
			(ssize_t)3);
	KUNIT_EXPECT_EQ(test, sbi->s_mb_group_prealloc, 256U);
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "mb_group_prealloc",
						   "257", 3),
			(ssize_t)-EINVAL);

	/* err_report_sec: <= 365 * 24 * 60 * 60 (31536000) */
	sbi->s_err_report_sec = 0;
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_report_sec",
						   "3600", 4),
			(ssize_t)4);
	KUNIT_EXPECT_EQ(test, sbi->s_err_report_sec, 3600UL);

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_report_sec",
						   "31536000", 8),
			(ssize_t)8);
	KUNIT_EXPECT_EQ(test, sbi->s_err_report_sec, 31536000UL);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "err_report_sec", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "31536000\n");

	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "err_report_sec",
						   "31536001", 8),
			(ssize_t)-EINVAL);
}

static void test_computed_ro_and_error_time_attrs(struct kunit *test)
{
	struct sysfs_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;
	struct ext4_super_block *es = &ctx->es;
	char expected_pid[32];
	char *buf;

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	/* 7 dirty clusters * 4 blocks/cluster == 28 delayed allocation blks */
	percpu_counter_set(&sbi->s_dirtyclusters_counter, 7);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi,
				"delayed_allocation_blocks", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "28\n");

	/* (50 - 10) sectors >> 1 == 20 KB session, + 100 == 120 KB lifetime */
	sbi->s_sectors_written_start = 10;
	sbi->s_kbytes_written = 100;
	part_stat_lock();
	part_stat_add(&ctx->bdev, sectors[STAT_WRITE], 50);
	part_stat_unlock();

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "session_write_kbytes",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "20\n");

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "lifetime_write_kbytes",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "120\n");

	/* sra_exceeded_retry_limit sums percpu counter */
	percpu_counter_set(&sbi->s_sra_exceeded_retry_limit, 13);
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi,
				"sra_exceeded_retry_limit", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "13\n");

	/* 40-bit timestamp formatting: (2 << 32) + 100 == 8589934692 */
	es->s_first_error_time = cpu_to_le32(100);
	es->s_first_error_time_hi = 2;
	es->s_last_error_time = cpu_to_le32(250);
	es->s_last_error_time_hi = 0;

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "first_error_time",
						  buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "8589934692\n");

	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "last_error_time", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "250\n");

	/* journal_task emits "<none>\n" without journal, or j_task PID */
	sbi->s_journal = NULL;
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "journal_task", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, "<none>\n");

	ctx->mock_journal.j_task = current;
	sbi->s_journal = &ctx->mock_journal;
	snprintf(expected_pid, sizeof(expected_pid), "%d\n",
		 task_pid_vnr(current));
	KUNIT_EXPECT_GT(test,
			ext4_sysfs_attr_show_test(sbi, "journal_task", buf),
			(ssize_t)0);
	KUNIT_EXPECT_STREQ(test, buf, expected_pid);
	sbi->s_journal = NULL;
}

static void test_feature_attrs_and_trigger_error(struct kunit *test)
{
	struct sysfs_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;
	static const char * const feat_names[] = {
		"lazy_itable_init",
		"batched_discard",
		"meta_bg_resize",
		"metadata_csum_seed",
		"fast_commit",
#ifdef CONFIG_FS_ENCRYPTION
		"encryption",
		"test_dummy_encryption_v2",
#endif
#if IS_ENABLED(CONFIG_UNICODE)
		"casefold",
#endif
#ifdef CONFIG_FS_VERITY
		"verity",
#endif
#if IS_ENABLED(CONFIG_UNICODE) && defined(CONFIG_FS_ENCRYPTION)
		"encrypted_casefold",
#endif
	};
	char *buf;
	size_t i;

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	for (i = 0; i < ARRAY_SIZE(feat_names); i++) {
		KUNIT_EXPECT_GT_MSG(test,
				    ext4_sysfs_feat_show_test(feat_names[i],
							      buf),
				    (ssize_t)0, "feat=%s", feat_names[i]);
		KUNIT_EXPECT_STREQ_MSG(test, buf, "supported\n",
				       "feat=%s", feat_names[i]);
	}
	KUNIT_EXPECT_EQ(test, ext4_sysfs_feat_show_test("unknown_feat", buf),
			(ssize_t)-ENOENT);

	/* Empty newline write to trigger_fs_error is a no-op */
	sbi->s_journal = &ctx->mock_journal;
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "trigger_fs_error",
						   "\n", 1),
			(ssize_t)1);
	KUNIT_EXPECT_EQ(test, sbi->s_mount_state & EXT4_ERROR_FS, 0);

	/* Non-empty message records error state via ext4_error() */
	KUNIT_EXPECT_EQ(test,
			ext4_sysfs_attr_store_test(sbi, "trigger_fs_error",
						   "kunit injected\n", 15),
			(ssize_t)15);
	KUNIT_EXPECT_NE(test, sbi->s_mount_state & EXT4_ERROR_FS, 0);
	if (IS_ENABLED(CONFIG_PRINTK))
		KUNIT_EXPECT_STREQ(test, sbi->s_last_error_func,
				   "trigger_test_error");
	else
		KUNIT_EXPECT_STREQ(test, sbi->s_last_error_func, "");
	sbi->s_journal = NULL;
}

static void test_sysfs_register_notify_and_unregister(struct kunit *test)
{
	struct sysfs_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;

	KUNIT_ASSERT_EQ(test, ext4_register_sysfs(sb), 0);
	KUNIT_EXPECT_EQ(test, (int)sbi->s_kobj.state_in_sysfs, 1);

	ext4_notify_error_sysfs(sbi);

	ext4_unregister_sysfs(sb);
	KUNIT_EXPECT_EQ(test, (int)sbi->s_kobj.state_in_sysfs, 0);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
}

static struct kunit_case ext4_sysfs_test_cases[] = {
	KUNIT_CASE(test_generic_pointer_attrs),
	KUNIT_CASE(test_validated_rw_attrs),
	KUNIT_CASE(test_computed_ro_and_error_time_attrs),
	KUNIT_CASE(test_feature_attrs_and_trigger_error),
	KUNIT_CASE(test_sysfs_register_notify_and_unregister),
	{}
};

static struct kunit_suite ext4_sysfs_test_suite = {
	.name = "ext4_sysfs_test",
	.init = sysfs_test_init,
	.test_cases = ext4_sysfs_test_cases,
};

kunit_test_suites(&ext4_sysfs_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 sysfs and procfs interfaces");
MODULE_LICENSE("GPL");
