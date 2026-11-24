// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/balloc.c
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/uidgid.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "mballoc.h"

/*
 * Default multi-group mock filesystem geometry:
 *  - 4 active block groups (capacity up to 32 groups across 2 GDT blocks)
 *  - 1024-byte blocks (8192 bits per bitmap block, 16 64-byte GDs per block)
 *  - s_first_data_block = 1, 1024 blocks (clusters) per group (total 4097)
 *  - 64 inodes per group, 16 inode table blocks per group
 *  - sparse_super + metadata_csum + 64bit enabled, s_gdb_count = 1:
 *    - Groups 0, 1, 3 have super + 1 gdb (2 base blocks) + bbitmap + ibitmap +
 *      16 itable blocks = 20 overhead clusters -> 1004 free clusters.
 *    - Group 2 has 0 base blocks + bbitmap + ibitmap + 16 itable blocks =
 *      18 overhead clusters -> 1006 free clusters.
 */
#define BALLOC_TEST_NGROUPS		4U
#define BALLOC_TEST_MAX_GROUPS		32U
#define BALLOC_TEST_GDESC_BLKS		2U
#define BALLOC_TEST_BLK_SIZE		1024U
#define BALLOC_TEST_BLK_BITS		10U
#define BALLOC_TEST_BLOCKS_PER_GRP	1024U
#define BALLOC_TEST_FIRST_DATA_BLK	1U
#define BALLOC_TEST_TOTAL_BLKS		(BALLOC_TEST_FIRST_DATA_BLK + \
					 BALLOC_TEST_NGROUPS * \
					 BALLOC_TEST_BLOCKS_PER_GRP)
#define BALLOC_TEST_INODE_SIZE		256U
#define BALLOC_TEST_INODES_PER_GRP	64U
#define BALLOC_TEST_ITB_PER_GRP		16U
#define BALLOC_TEST_CSUM_SEED		0x5a3c91e7U
#define BALLOC_TEST_GDESC_BLK0		2ULL
#define BALLOC_TEST_POOL_SIZE		16U

struct balloc_test_blk {
	bool in_use;
	ext4_fsblk_t blocknr;
	struct buffer_head bh;
	char cache_buf[BALLOC_TEST_BLK_SIZE] __aligned(8);
	char disk_buf[BALLOC_TEST_BLK_SIZE] __aligned(8);
};

struct balloc_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct blockgroup_lock bgl;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct buffer_head sb_bh;

	struct ext4_group_desc gdps[BALLOC_TEST_MAX_GROUPS] __aligned(8);
	struct buffer_head gdesc_bh[BALLOC_TEST_GDESC_BLKS];
	struct buffer_head *gdesc_bh_ptrs[BALLOC_TEST_GDESC_BLKS];

	struct ext4_group_info *grp_infos[BALLOC_TEST_MAX_GROUPS];
	struct ext4_group_info *grp_info_slice0[16];
	struct ext4_group_info *grp_info_slice1[16];
	struct ext4_group_info **grp_info_slices[BALLOC_TEST_GDESC_BLKS];

	struct inode *test_inode;
	journal_t journal;

	struct balloc_test_blk pool[BALLOC_TEST_POOL_SIZE];

	bool fcc_inited;
	bool dcc_inited;
	bool fic_inited;
	bool sra_inited;

	/* Credential override tracking for deterministic cleanup */
	const struct cred *saved_cred;
	struct cred *override_cred;

	/* Discard work tracking */
	unsigned int discard_work_runs;
	int discard_saw_retry_pending;
	s64 discard_release_clusters;
	bool discard_wait_retry_pending;

	/* Stub fault injection and call capture */
	bool fail_getblk;
	bool fail_read_eio;
	unsigned int read_nowait_calls;
	blk_opf_t last_read_op_flags;

	unsigned int mb_new_blocks_calls;
	struct ext4_allocation_request last_ar;
	ext4_fsblk_t mock_alloc_blk;
	unsigned int mock_alloc_len;
	int mock_alloc_err;
};

static inline ext4_fsblk_t balloc_test_group_start(ext4_group_t g)
{
	return BALLOC_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * BALLOC_TEST_BLOCKS_PER_GRP;
}

static inline unsigned int balloc_test_default_base_blks(ext4_group_t g)
{
	return (g == 2) ? 0U : 2U;
}

static inline ext4_fsblk_t balloc_test_bbitmap_blk(ext4_group_t g)
{
	return balloc_test_group_start(g) + balloc_test_default_base_blks(g);
}

static inline ext4_fsblk_t balloc_test_ibitmap_blk(ext4_group_t g)
{
	return balloc_test_bbitmap_blk(g) + 1;
}

static inline ext4_fsblk_t balloc_test_itable_blk(ext4_group_t g)
{
	return balloc_test_bbitmap_blk(g) + 2;
}

static inline void clear_bitmap_uptodate(struct buffer_head *bh)
{
	clear_bit(BH_BITMAP_UPTODATE, &bh->b_state);
}

static struct inode *balloc_test_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	init_rwsem(&ei->xattr_sem);
	INIT_LIST_HEAD(&ei->i_orphan);
	inode_init_once(&ei->vfs_inode);
	ext4_fc_init_inode(&ei->vfs_inode);
	spin_lock_init(&ei->i_raw_lock);
	spin_lock_init(&ei->i_block_reservation_lock);
#ifdef CONFIG_QUOTA
	memset(&ei->i_dquot, 0, sizeof(ei->i_dquot));
#endif
	return &ei->vfs_inode;
}

static void balloc_test_free_inode_mem(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **balloc_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *balloc_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations balloc_test_qops = {
	.get_reserved_space = balloc_test_get_reserved_space,
};
#endif

static const struct super_operations balloc_test_sops = {
	.alloc_inode	= balloc_test_alloc_inode,
	.free_inode	= balloc_test_free_inode_mem,
	.drop_inode	= inode_just_drop,
#ifdef CONFIG_QUOTA
	.get_dquots	= balloc_test_get_dquots,
#endif
};

static int balloc_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type balloc_test_fs_type = {
	.name		 = "ext4_balloc_kunit",
	.init_fs_context = balloc_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static struct balloc_test_blk *balloc_test_find_blk(struct balloc_test_ctx *ctx,
						    ext4_fsblk_t blocknr)
{
	unsigned int i;

	for (i = 0; i < BALLOC_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && ctx->pool[i].blocknr == blocknr)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct balloc_test_blk *
balloc_test_find_blk_by_bh(struct balloc_test_ctx *ctx, struct buffer_head *bh)
{
	unsigned int i;

	for (i = 0; i < BALLOC_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && &ctx->pool[i].bh == bh)
			return &ctx->pool[i];
	}
	return NULL;
}

static struct balloc_test_blk *
balloc_test_setup_blk(struct balloc_test_ctx *ctx, ext4_fsblk_t blocknr,
		      bool uptodate)
{
	struct balloc_test_blk *slot = balloc_test_find_blk(ctx, blocknr);
	unsigned int i;

	if (!slot) {
		for (i = 0; i < BALLOC_TEST_POOL_SIZE; i++) {
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
	slot->blocknr = blocknr;
	slot->bh.b_bdev = &ctx->bdev;
	slot->bh.b_blocknr = blocknr;
	slot->bh.b_size = BALLOC_TEST_BLK_SIZE;
	slot->bh.b_data = slot->cache_buf;
	atomic_set(&slot->bh.b_count, 1);
	if (uptodate)
		set_buffer_uptodate(&slot->bh);
	return slot;
}

static void balloc_test_sync_bbitmap_csum(struct balloc_test_ctx *ctx,
					  ext4_group_t g, bool sync_disk)
{
	struct ext4_group_desc *gdp = &ctx->gdps[g];
	struct balloc_test_blk *slot;

	slot = balloc_test_find_blk(ctx, ext4_block_bitmap(ctx->sb, gdp));
	if (slot) {
		ext4_block_bitmap_csum_set(ctx->sb, gdp, &slot->bh);
		if (sync_disk)
			memcpy(slot->disk_buf, slot->cache_buf,
			       BALLOC_TEST_BLK_SIZE);
	}
	ext4_group_desc_csum_set(ctx->sb, g, gdp);
}

static void balloc_test_discard_work_fn(struct work_struct *work)
{
	struct ext4_sb_info *sbi =
		container_of(work, struct ext4_sb_info, s_discard_work);
	struct balloc_test_ctx *ctx =
		container_of(sbi, struct balloc_test_ctx, sbi);

	if (ctx->discard_wait_retry_pending) {
		ktime_t deadline = ktime_add_ms(ktime_get(), 500);

		while (!atomic_read(&sbi->s_retry_alloc_pending) &&
		       ktime_before(ktime_get(), deadline))
			usleep_range(100, 500);
	}

	ctx->discard_work_runs++;
	ctx->discard_saw_retry_pending = atomic_read(&sbi->s_retry_alloc_pending);
	if (ctx->discard_release_clusters)
		percpu_counter_add(&sbi->s_freeclusters_counter,
				   ctx->discard_release_clusters);
}

static void balloc_test_restore_creds(struct balloc_test_ctx *ctx)
{
	if (ctx->saved_cred) {
		revert_creds(ctx->saved_cred);
		ctx->saved_cred = NULL;
	}
	if (ctx->override_cred) {
		put_cred(ctx->override_cred);
		ctx->override_cred = NULL;
	}
}

static void balloc_test_override_creds(struct kunit *test,
				       struct balloc_test_ctx *ctx,
				       uid_t uid, gid_t gid, bool cap_sys_res,
				       int supp_gid)
{
	struct cred *cred;

	balloc_test_restore_creds(ctx);

	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	ctx->override_cred = cred;

	cred->uid = KUIDT_INIT(uid);
	cred->euid = KUIDT_INIT(uid);
	cred->suid = KUIDT_INIT(uid);
	cred->fsuid = KUIDT_INIT(uid);
	cred->gid = KGIDT_INIT(gid);
	cred->egid = KGIDT_INIT(gid);
	cred->sgid = KGIDT_INIT(gid);
	cred->fsgid = KGIDT_INIT(gid);
	cap_clear(cred->cap_effective);
	cap_clear(cred->cap_permitted);
	cap_clear(cred->cap_inheritable);
	cap_clear(cred->cap_ambient);
	if (cap_sys_res) {
		cap_raise(cred->cap_permitted, CAP_SYS_RESOURCE);
		cap_raise(cred->cap_effective, CAP_SYS_RESOURCE);
	}
#ifdef CONFIG_MULTIUSER
	{
		struct group_info *gi = groups_alloc(supp_gid >= 0 ? 1 : 0);

		KUNIT_ASSERT_NOT_NULL(test, gi);
		if (supp_gid >= 0) {
			gi->gid[0] = KGIDT_INIT((gid_t)supp_gid);
			groups_sort(gi);
		}
		set_groups(cred, gi);
		put_group_info(gi);
	}
#endif
	ctx->saved_cred = override_creds(cred);
}

/* --- Static Stubs --- */

static struct buffer_head *balloc_test_sb_getblk_stub(struct super_block *sb,
						      ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct balloc_test_ctx *ctx = test ? test->priv : NULL;
	struct balloc_test_blk *slot;

	if (!ctx || ctx->fail_getblk)
		return NULL;
	slot = balloc_test_find_blk(ctx, block);
	if (!slot)
		return NULL;
	get_bh(&slot->bh);
	return &slot->bh;
}

static void balloc_test_read_bh_nowait_stub(struct buffer_head *bh,
					    blk_opf_t op_flags,
					    bio_end_io_t end_io,
					    bool simu_fail)
{
	struct kunit *test = kunit_get_current_test();
	struct balloc_test_ctx *ctx = test ? test->priv : NULL;
	struct balloc_test_blk *slot;
	bool fail;

	if (!ctx) {
		unlock_buffer(bh);
		return;
	}
	ctx->read_nowait_calls++;
	ctx->last_read_op_flags = op_flags;

	if (simu_fail) {
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		return;
	}

	clear_buffer_verified(bh);
	slot = balloc_test_find_blk_by_bh(ctx, bh);
	fail = ctx->fail_read_eio || !slot;
	if (!fail)
		memcpy(bh->b_data, slot->disk_buf, bh->b_size);

	if (end_io) {
		struct bio *bio = bio_alloc(NULL, 0, REQ_OP_READ, GFP_KERNEL);

		if (bio) {
			bio->bi_private = bh;
			bio->bi_status = fail ? BLK_STS_IOERR : BLK_STS_OK;
			end_io(bio);
			return;
		}
	}

	if (fail) {
		clear_buffer_uptodate(bh);
	} else {
		set_buffer_uptodate(bh);
		set_bitmap_uptodate(bh);
	}
	unlock_buffer(bh);
}

static ext4_fsblk_t
balloc_test_mb_new_blocks_stub(handle_t *handle,
			       struct ext4_allocation_request *ar, int *errp)
{
	struct kunit *test = kunit_get_current_test();
	struct balloc_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx) {
		*errp = -EIO;
		return 0;
	}
	ctx->mb_new_blocks_calls++;
	ctx->last_ar = *ar;
	*errp = ctx->mock_alloc_err;
	if (ctx->mock_alloc_err)
		return 0;
	if (ctx->mock_alloc_len)
		ar->len = ctx->mock_alloc_len;
	return ctx->mock_alloc_blk;
}

/* --- Fixture Setup & Teardown --- */

static void balloc_test_cleanup(struct balloc_test_ctx *ctx)
{
	unsigned int i;

	balloc_test_restore_creds(ctx);

	for (i = 0; i < BALLOC_TEST_POOL_SIZE; i++) {
		if (ctx->pool[i].in_use && buffer_locked(&ctx->pool[i].bh))
			unlock_buffer(&ctx->pool[i].bh);
	}

	cancel_work_sync(&ctx->sbi.s_discard_work);
	ctx->sbi.s_journal = NULL;

	if (ctx->test_inode) {
		spin_lock(&ctx->test_inode->i_lock);
		inode_state_clear(ctx->test_inode, I_DIRTY_ALL);
		spin_unlock(&ctx->test_inode->i_lock);
		iput(ctx->test_inode);
		ctx->test_inode = NULL;
	}

	if (ctx->sb) {
		deactivate_super(ctx->sb);
		rcu_barrier();
		ctx->sb = NULL;
	}

	if (ctx->sra_inited)
		percpu_counter_destroy(&ctx->sbi.s_sra_exceeded_retry_limit);
	if (ctx->fic_inited)
		percpu_counter_destroy(&ctx->sbi.s_freeinodes_counter);
	if (ctx->dcc_inited)
		percpu_counter_destroy(&ctx->sbi.s_dirtyclusters_counter);
	if (ctx->fcc_inited)
		percpu_counter_destroy(&ctx->sbi.s_freeclusters_counter);

	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(balloc_test_cleanup_action, balloc_test_cleanup,
			    struct balloc_test_ctx *);

static int balloc_test_init(struct kunit *test)
{
	struct balloc_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	ext4_group_t g;
	s64 total_free_clusters = 0;
	s64 total_free_inodes = 0;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	for (g = 0; g < BALLOC_TEST_MAX_GROUPS; g++) {
		ctx->grp_infos[g] = kunit_kzalloc(test,
						  sizeof(*ctx->grp_infos[g]),
						  GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, ctx->grp_infos[g]);
	}

	INIT_WORK(&ctx->sbi.s_discard_work, balloc_test_discard_work_fn);
	rwlock_init(&ctx->journal.j_state_lock);

	err = kunit_add_action_or_reset(test, balloc_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	fc = fs_context_for_mount(&balloc_test_fs_type, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(fc));
	sb = sget_fc(fc, NULL, set_anon_super_fc);
	put_fs_context(fc);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb));

	sbi = &ctx->sbi;
	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &balloc_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &balloc_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = BALLOC_TEST_BLK_SIZE;
	sb->s_blocksize_bits = BALLOC_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	strscpy(sb->s_id, "ext4_balloc_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = BALLOC_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	root_inode = new_inode(sb);
	if (!root_inode) {
		deactivate_locked_super(sb);
		rcu_barrier();
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc root_inode");
	}
	root_inode->i_ino = EXT4_ROOT_INO;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_op = &simple_dir_inode_operations;
	set_nlink(root_inode, 2);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		deactivate_locked_super(sb);
		rcu_barrier();
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc s_root");
	}

	up_write(&sb->s_umount);
	ctx->sb = sb;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(BALLOC_TEST_FIRST_DATA_BLK);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_inodes_count = cpu_to_le32(BALLOC_TEST_NGROUPS *
					     BALLOC_TEST_INODES_PER_GRP);
	ctx->es.s_inodes_per_group = cpu_to_le32(BALLOC_TEST_INODES_PER_GRP);
	ctx->es.s_blocks_per_group = cpu_to_le32(BALLOC_TEST_BLOCKS_PER_GRP);
	ctx->es.s_clusters_per_group = cpu_to_le32(BALLOC_TEST_BLOCKS_PER_GRP);
	ctx->es.s_inode_size = cpu_to_le16(BALLOC_TEST_INODE_SIZE);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_blocks_count_set(&ctx->es, BALLOC_TEST_TOTAL_BLKS);

	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_extents(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	for (g = 0; g < BALLOC_TEST_GDESC_BLKS; g++) {
		ctx->gdesc_bh[g].b_data = (char *)&ctx->gdps[g * 16];
		ctx->gdesc_bh[g].b_size = BALLOC_TEST_BLK_SIZE;
		ctx->gdesc_bh[g].b_blocknr = BALLOC_TEST_GDESC_BLK0 + g;
		ctx->gdesc_bh[g].b_bdev = &ctx->bdev;
		atomic_set(&ctx->gdesc_bh[g].b_count, 1);
		set_buffer_uptodate(&ctx->gdesc_bh[g]);
		ctx->gdesc_bh_ptrs[g] = &ctx->gdesc_bh[g];
	}
	rcu_assign_pointer(sbi->s_group_desc, ctx->gdesc_bh_ptrs);

	sbi->s_inode_size = BALLOC_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = BALLOC_TEST_BLK_SIZE / BALLOC_TEST_INODE_SIZE;
	sbi->s_inodes_per_group = BALLOC_TEST_INODES_PER_GRP;
	sbi->s_itb_per_group = BALLOC_TEST_ITB_PER_GRP;
	sbi->s_blocks_per_group = BALLOC_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = BALLOC_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = BALLOC_TEST_NGROUPS;
	sbi->s_gdb_count = 1;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = BALLOC_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block_bits = ilog2(sbi->s_desc_per_block);
	sbi->s_csum_seed = BALLOC_TEST_CSUM_SEED;
	sbi->s_resuid = make_kuid(&init_user_ns, EXT4_DEF_RESUID);
	sbi->s_resgid = make_kgid(&init_user_ns, EXT4_DEF_RESGID);

	set_opt(sb, ERRORS_CONT);
	set_opt(sb, DELALLOC);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);

	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	for (g = 0; g < BALLOC_TEST_MAX_GROUPS; g++) {
		init_rwsem(&ctx->grp_infos[g]->alloc_sem);
		ctx->grp_infos[g]->bb_state = 0;
		if (g < 16)
			ctx->grp_info_slice0[g] = ctx->grp_infos[g];
		else
			ctx->grp_info_slice1[g - 16] = ctx->grp_infos[g];
	}
	ctx->grp_info_slices[0] = ctx->grp_info_slice0;
	ctx->grp_info_slices[1] = ctx->grp_info_slice1;
	rcu_assign_pointer(sbi->s_group_info, ctx->grp_info_slices);

	for (g = 0; g < BALLOC_TEST_NGROUPS; g++) {
		struct balloc_test_blk *bblk;
		unsigned int overhead = balloc_test_default_base_blks(g) +
					2U + BALLOC_TEST_ITB_PER_GRP;
		u32 free_clu = BALLOC_TEST_BLOCKS_PER_GRP - overhead;
		unsigned int bit;

		ext4_block_bitmap_set(sb, &ctx->gdps[g],
				      balloc_test_bbitmap_blk(g));
		ext4_inode_bitmap_set(sb, &ctx->gdps[g],
				      balloc_test_ibitmap_blk(g));
		ext4_inode_table_set(sb, &ctx->gdps[g],
				     balloc_test_itable_blk(g));
		ext4_free_group_clusters_set(sb, &ctx->gdps[g], free_clu);
		ext4_free_inodes_set(sb, &ctx->gdps[g],
				     BALLOC_TEST_INODES_PER_GRP);
		ctx->grp_infos[g]->bb_free = free_clu;

		bblk = balloc_test_setup_blk(ctx, balloc_test_bbitmap_blk(g),
					     true);
		KUNIT_ASSERT_NOT_NULL(test, bblk);

		for (bit = 0; bit < overhead; bit++)
			ext4_set_bit(bit, bblk->cache_buf);
		ext4_mark_bitmap_end(BALLOC_TEST_BLOCKS_PER_GRP,
				     BALLOC_TEST_BLK_SIZE * 8,
				     bblk->cache_buf);
		set_bitmap_uptodate(&bblk->bh);
		set_buffer_verified(&bblk->bh);
		balloc_test_sync_bbitmap_csum(ctx, g, true);

		total_free_clusters += free_clu;
		total_free_inodes += BALLOC_TEST_INODES_PER_GRP;
	}

	err = percpu_counter_init(&sbi->s_freeclusters_counter,
				  total_free_clusters, GFP_KERNEL);
	if (!err)
		ctx->fcc_inited = true;
	KUNIT_ASSERT_EQ(test, err, 0);

	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0, GFP_KERNEL);
	if (!err)
		ctx->dcc_inited = true;
	KUNIT_ASSERT_EQ(test, err, 0);

	err = percpu_counter_init(&sbi->s_freeinodes_counter,
				  total_free_inodes, GFP_KERNEL);
	if (!err)
		ctx->fic_inited = true;
	KUNIT_ASSERT_EQ(test, err, 0);

	err = percpu_counter_init(&sbi->s_sra_exceeded_retry_limit, 0,
				  GFP_KERNEL);
	if (!err)
		ctx->sra_inited = true;
	KUNIT_ASSERT_EQ(test, err, 0);

	ctx->test_inode = new_inode(sb);
	KUNIT_ASSERT_NOT_NULL(test, ctx->test_inode);
	ctx->test_inode->i_ino = EXT4_FIRST_INO(sb);
	ctx->test_inode->i_mode = S_IFREG | 0644;
	ctx->test_inode->i_blkbits = BALLOC_TEST_BLK_BITS;
	inode_set_bytes(ctx->test_inode, 0);
	EXT4_I(ctx->test_inode)->i_block_group = 0;

	kunit_activate_static_stub(test, ext4_sb_getblk,
				   balloc_test_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_read_bh_nowait,
				   balloc_test_read_bh_nowait_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   balloc_test_mb_new_blocks_stub);

	return 0;
}

/* =========================================================================
 * 1. Group Number & Offset Calculation
 * =========================================================================
 */

struct group_offset_param {
	const char *desc;
	unsigned char blocksize_bits;
	unsigned int cluster_bits;
	u32 blocks_per_group;
	u32 first_data_block;
	bool std_group_size;
	ext4_fsblk_t block;
	ext4_group_t expected_group;
	ext4_grpblk_t expected_offset;
};

static const struct group_offset_param group_offset_params[] = {
	{
		.desc = "std_1k_bg0_first_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 8192,
		.first_data_block = 1,
		.std_group_size = true,
		.block = 1,
		.expected_group = 0,
		.expected_offset = 0,
	},
	{
		.desc = "std_1k_bg0_last_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 8192,
		.first_data_block = 1,
		.std_group_size = true,
		.block = 8192,
		.expected_group = 0,
		.expected_offset = 8191,
	},
	{
		.desc = "std_1k_bg1_first_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 8192,
		.first_data_block = 1,
		.std_group_size = true,
		.block = 8193,
		.expected_group = 1,
		.expected_offset = 0,
	},
	{
		.desc = "std_1k_bg3_interior_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 8192,
		.first_data_block = 1,
		.std_group_size = true,
		.block = 25000,
		.expected_group = 3,
		.expected_offset = 423,
	},
	{
		.desc = "std_4k_bg0_last_block",
		.blocksize_bits = 12,
		.cluster_bits = 0,
		.blocks_per_group = 32768,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 32767,
		.expected_group = 0,
		.expected_offset = 32767,
	},
	{
		.desc = "std_4k_bg1_first_block",
		.blocksize_bits = 12,
		.cluster_bits = 0,
		.blocks_per_group = 32768,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 32768,
		.expected_group = 1,
		.expected_offset = 0,
	},
	{
		.desc = "std_4k_above_32bit_blocknr",
		.blocksize_bits = 12,
		.cluster_bits = 0,
		.blocks_per_group = 32768,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 0x100008005ULL,
		.expected_group = 131073,
		.expected_offset = 5,
	},
	{
		.desc = "nonstd_bg0_last_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 1000,
		.first_data_block = 1,
		.std_group_size = false,
		.block = 1000,
		.expected_group = 0,
		.expected_offset = 999,
	},
	{
		.desc = "nonstd_bg1_first_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 1000,
		.first_data_block = 1,
		.std_group_size = false,
		.block = 1001,
		.expected_group = 1,
		.expected_offset = 0,
	},
	{
		.desc = "nonstd_bg2_interior_block",
		.blocksize_bits = 10,
		.cluster_bits = 0,
		.blocks_per_group = 1000,
		.first_data_block = 1,
		.std_group_size = false,
		.block = 2346,
		.expected_group = 2,
		.expected_offset = 345,
	},
	{
		.desc = "bigalloc_std_cluster_start",
		.blocksize_bits = 12,
		.cluster_bits = 3,
		.blocks_per_group = 262144,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 16,
		.expected_group = 0,
		.expected_offset = 2,
	},
	{
		.desc = "bigalloc_std_cluster_end_same_offset",
		.blocksize_bits = 12,
		.cluster_bits = 3,
		.blocks_per_group = 262144,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 23,
		.expected_group = 0,
		.expected_offset = 2,
	},
	{
		.desc = "bigalloc_std_next_cluster",
		.blocksize_bits = 12,
		.cluster_bits = 3,
		.blocks_per_group = 262144,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 24,
		.expected_group = 0,
		.expected_offset = 3,
	},
	{
		.desc = "bigalloc_std_bg2_offset",
		.blocksize_bits = 12,
		.cluster_bits = 3,
		.blocks_per_group = 262144,
		.first_data_block = 0,
		.std_group_size = true,
		.block = 524303,
		.expected_group = 2,
		.expected_offset = 1,
	},
	{
		.desc = "bigalloc_nonstd_bg3_offset",
		.blocksize_bits = 10,
		.cluster_bits = 2,
		.blocks_per_group = 600,
		.first_data_block = 1,
		.std_group_size = false,
		.block = 1820,
		.expected_group = 3,
		.expected_offset = 4,
	},
};

static void group_offset_param_get_desc(const struct group_offset_param *p,
					char *desc)
{
	strscpy(desc, p->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(group_offset, group_offset_params,
		  group_offset_param_get_desc);

static void test_group_no_and_offset_matrix(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	const struct group_offset_param *p = test->param_value;
	ext4_group_t grp = 0xdeadbeefU;
	ext4_grpblk_t off = 0x7654321;

	sb->s_blocksize_bits = p->blocksize_bits;
	sb->s_blocksize = 1UL << p->blocksize_bits;
	sbi->s_cluster_bits = p->cluster_bits;
	sbi->s_cluster_ratio = 1U << p->cluster_bits;
	sbi->s_blocks_per_group = p->blocks_per_group;
	sbi->s_clusters_per_group = p->blocks_per_group >> p->cluster_bits;
	ctx->es.s_first_data_block = cpu_to_le32(p->first_data_block);
	if (p->std_group_size)
		set_opt2(sb, STD_GROUP_SIZE);
	else
		clear_opt2(sb, STD_GROUP_SIZE);

	KUNIT_EXPECT_EQ(test, ext4_get_group_number(sb, p->block),
			p->expected_group);

	ext4_get_group_no_and_offset(sb, p->block, &grp, &off);
	KUNIT_EXPECT_EQ(test, grp, p->expected_group);
	KUNIT_EXPECT_EQ(test, off, p->expected_offset);
}

static void test_group_no_and_offset_optional_out_ptrs(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	ext4_group_t grp = 999;
	ext4_grpblk_t off = 999;

	/* Block 2060 = first_data_block (1) + 2 * 1024 + 11 */
	ext4_get_group_no_and_offset(sb, 2060, &grp, NULL);
	KUNIT_EXPECT_EQ(test, grp, 2U);

	ext4_get_group_no_and_offset(sb, 2060, NULL, &off);
	KUNIT_EXPECT_EQ(test, off, 11);

	/* Both output pointers NULL must be a safe no-op */
	ext4_get_group_no_and_offset(sb, 2060, NULL, NULL);
}

/* =========================================================================
 * 2. Backup Superblock Placement (ext4_bg_has_super)
 * =========================================================================
 */

static void test_bg_has_super_dense_all_groups(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	static const ext4_group_t groups[] = { 0, 1, 2, 3, 4, 6, 8, 15, 22 };
	unsigned int i;

	ext4_clear_feature_sparse_super(sb);
	ext4_clear_feature_sparse_super2(sb);

	for (i = 0; i < ARRAY_SIZE(groups); i++)
		KUNIT_EXPECT_EQ_MSG(test, ext4_bg_has_super(sb, groups[i]), 1,
				    "dense group %u should have super",
				    groups[i]);
}

static void test_bg_has_super_sparse_super_powers(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	static const ext4_group_t with_super[] = {
		0, 1, 3, 9, 27, 81, 5, 25, 125, 7, 49, 343
	};
	static const ext4_group_t without_super[] = {
		2, 4, 6, 8, 10, 11, 15, 21, 35, 45, 63, 77
	};
	unsigned int i;

	ext4_set_feature_sparse_super(sb);
	ext4_clear_feature_sparse_super2(sb);

	for (i = 0; i < ARRAY_SIZE(with_super); i++)
		KUNIT_EXPECT_EQ_MSG(test, ext4_bg_has_super(sb, with_super[i]),
				    1, "sparse_super group %u expected 1",
				    with_super[i]);

	for (i = 0; i < ARRAY_SIZE(without_super); i++)
		KUNIT_EXPECT_EQ_MSG(test,
				    ext4_bg_has_super(sb, without_super[i]), 0,
				    "sparse_super group %u expected 0",
				    without_super[i]);
}

static void test_bg_has_super_sparse_super2_explicit_backups(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	ext4_set_feature_sparse_super2(sb);
	ctx->es.s_backup_bgs[0] = cpu_to_le32(6);
	ctx->es.s_backup_bgs[1] = cpu_to_le32(42);

	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 0), 1);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 6), 1);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 42), 1);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 3), 0);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 5), 0);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 7), 0);

	/* Single backup group configured (s_backup_bgs[1] == 0) */
	ctx->es.s_backup_bgs[0] = cpu_to_le32(12);
	ctx->es.s_backup_bgs[1] = cpu_to_le32(0);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 0), 1);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 12), 1);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 6), 0);
	KUNIT_EXPECT_EQ(test, ext4_bg_has_super(sb, 42), 0);
}

/* =========================================================================
 * 3. GDT & Base Metadata Blocks (ext4_bg_num_gdb, ext4_num_base_meta_blocks)
 * =========================================================================
 */

static void test_bg_num_gdb_and_base_meta_non_metabg(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	ext4_clear_feature_meta_bg(sb);
	ext4_set_feature_sparse_super(sb);
	ctx->sbi.s_gdb_count = 4;
	ctx->es.s_reserved_gdt_blocks = cpu_to_le16(7);

	/* Groups with super: 1 super + 4 gdb + 7 reserved_gdt = 12 blocks */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 0), 4UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 0), 12U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 3), 4UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 3), 12U);

	/* Groups without super: 0 gdb and reserved_gdt is not present */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 2), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 2), 0U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 15), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 15), 0U);
}

static void test_bg_num_gdb_and_base_meta_hybrid_metabg(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	/*
	 * s_desc_per_block = 16, s_first_meta_bg = 2:
	 *  - Groups 0..31 (metagroups 0..1) use traditional GDT layout with
	 *    s_first_meta_bg (2) GDT blocks + s_reserved_gdt_blocks (9) when
	 *    backup super is present.
	 *  - Groups >= 32 (metagroups >= 2) place 1 descriptor block in the
	 *    1st, 2nd, and last group of each 16-group metagroup, with no
	 *    reserved GDT blocks.
	 */
	ext4_set_feature_meta_bg(sb);
	ext4_set_feature_sparse_super(sb);
	ctx->sbi.s_gdb_count = 5;
	ctx->es.s_first_meta_bg = cpu_to_le32(2);
	ctx->es.s_reserved_gdt_blocks = cpu_to_le16(9);

	/* Pre-meta_bg groups with super: 1 + 2 + 9 = 12 */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 0), 2UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 0), 12U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 27), 2UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 27), 12U);

	/* Pre-meta_bg groups without super */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 2), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 2), 0U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 31), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 31), 0U);

	/* Metagroup 2 (groups 32..47): first=32, second=33, last=47 (no super) */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 32), 1UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 32), 1U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 33), 1UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 33), 1U);
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 47), 1UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 47), 1U);

	/* Interior meta_bg group with neither super nor meta gdb */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 34), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 34), 0U);

	/* Group 125 (5^3 in metagroup 112..127): super=1, meta_gdb=0 -> 1 */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 125), 0UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 125), 1U);

	/* Group 49 (7^2, second of 48..63): super=1, meta_gdb=1 -> 2 */
	KUNIT_EXPECT_EQ(test, ext4_bg_num_gdb(sb, 49), 1UL);
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(sb, 49), 2U);
}

/* =========================================================================
 * 4. Overhead & Free Clusters After Init (ext4_free_clusters_after_init)
 * =========================================================================
 */

static void
test_free_clusters_after_init_standard_and_partial_last(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 0, &ctx->gdps[0]),
			1004U);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			1006U);

	/* Shrink total blocks so last group 3 has only 300 blocks (20 overhead) */
	ext4_blocks_count_set(&ctx->es, balloc_test_group_start(3) + 300);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 3, &ctx->gdps[3]),
			280U);
	/* Non-last group 1 still uses full s_blocks_per_group (1024) */
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 1, &ctx->gdps[1]),
			1004U);
}

static void
test_free_clusters_after_init_external_and_spanning_meta(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	ext4_fsblk_t g2_start = balloc_test_group_start(2);

	/*
	 * Group 2 has 0 base metadata blocks. Relocate its bitmaps and inode
	 * table into group 0 -> 0 overhead clusters in group 2 -> 1024 free.
	 */
	ext4_block_bitmap_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 50);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 51);
	ext4_inode_table_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 52);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			1024U);

	/*
	 * Inode table of 16 blocks starting 6 blocks before group 2 overlaps
	 * 10 blocks [g2_start .. g2_start + 9] inside group 2 -> 1014 free.
	 */
	ext4_inode_table_set(sb, &ctx->gdps[2], g2_start - 6);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			1014U);

	/*
	 * Inode table starting 5 blocks before end of group 2 overlaps 5
	 * blocks [g2_end - 4 .. g2_end] inside group 2 -> 1019 free.
	 */
	ext4_inode_table_set(sb, &ctx->gdps[2],
			     g2_start + BALLOC_TEST_BLOCKS_PER_GRP - 5);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			1019U);
}

static void
test_free_clusters_after_init_bigalloc_cluster_sharing(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	ext4_fsblk_t g1_start = balloc_test_group_start(1);
	ext4_fsblk_t g2_start = balloc_test_group_start(2);

	/* 4 blocks per cluster -> 1024 / 4 = 256 clusters per full group */
	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;
	sbi->s_clusters_per_group = 256;
	sbi->s_itb_per_group = 8;

	/*
	 * Case 1 (border overlap with base_clusters):
	 * Group 1 has 2 base blocks (offsets 0..1 -> base_clusters = 1, clu 0).
	 * Place 8-block itable at offsets 2..9 (clusters 0..2, overlapping
	 * border clu 0), bbitmap at offset 12 (clu 3), ibitmap at offset 16
	 * (clu 4) -> 5 unique overhead clusters (0..4) -> 251 free clusters.
	 */
	ext4_inode_table_set(sb, &ctx->gdps[1], g1_start + 2);
	ext4_block_bitmap_set(sb, &ctx->gdps[1], g1_start + 12);
	ext4_inode_bitmap_set(sb, &ctx->gdps[1], g1_start + 16);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 1, &ctx->gdps[1]),
			251U);

	/*
	 * Case 2 (bitmaps inside base_clusters):
	 * Base blocks at offsets 0..1 (clu 0), bbitmap at offset 2 (clu 0),
	 * ibitmap at offset 3 (clu 0), 8-block itable at offsets 4..11
	 * (clusters 1..2) -> 3 overhead clusters (0..2) -> 253 free clusters.
	 */
	ext4_block_bitmap_set(sb, &ctx->gdps[1], g1_start + 2);
	ext4_inode_bitmap_set(sb, &ctx->gdps[1], g1_start + 3);
	ext4_inode_table_set(sb, &ctx->gdps[1], g1_start + 4);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 1, &ctx->gdps[1]),
			253U);

	/*
	 * Case 3 (bbitmap and ibitmap sharing the same non-base cluster):
	 * Group 2 has 0 base clusters. bbitmap at offset 0 (clu 0), ibitmap at
	 * offset 1 (clu 0), 8-block itable at offsets 4..11 (clusters 1..2) ->
	 * 3 overhead clusters -> 253 free clusters.
	 */
	ext4_block_bitmap_set(sb, &ctx->gdps[2], g2_start + 0);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], g2_start + 1);
	ext4_inode_table_set(sb, &ctx->gdps[2], g2_start + 4);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			253U);

	/*
	 * Case 4 (bitmaps sharing tail cluster of inode table):
	 * Group 2 with 6-block itable at offsets 0..5 (clusters 0..1), bbitmap
	 * at offset 6 (clu 1), ibitmap at offset 7 (clu 1) -> 2 overhead
	 * clusters (0..1) -> 254 free clusters.
	 */
	sbi->s_itb_per_group = 6;
	ext4_inode_table_set(sb, &ctx->gdps[2], g2_start + 0);
	ext4_block_bitmap_set(sb, &ctx->gdps[2], g2_start + 6);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], g2_start + 7);
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 2, &ctx->gdps[2]),
			254U);

	/*
	 * Case 5 (partial last group rounding up blocks to clusters):
	 * Group 3 with 301 blocks -> EXT4_NUM_B2C(4, 301) = 76 clusters;
	 * external metadata -> 0 overhead in group 3 -> 76 free clusters.
	 */
	ext4_blocks_count_set(&ctx->es, balloc_test_group_start(3) + 301);
	ext4_block_bitmap_set(sb, &ctx->gdps[3], balloc_test_group_start(0) + 40);
	ext4_inode_bitmap_set(sb, &ctx->gdps[3], balloc_test_group_start(0) + 44);
	ext4_inode_table_set(sb, &ctx->gdps[3], balloc_test_group_start(0) + 48);
	/* Group 3 has 2 base blocks = 1 base cluster -> 76 - 1 = 75 free */
	KUNIT_EXPECT_EQ(test,
			ext4_free_clusters_after_init(sb, 3, &ctx->gdps[3]),
			75U);
}

/* =========================================================================
 * 5. Group Descriptor & Group Info Lookup
 * =========================================================================
 */

static void test_get_group_desc_lookup_and_bounds(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct buffer_head *bh = NULL;
	struct ext4_group_desc *desc;

	/* Expand to 20 groups spanning gdesc_bh[0] (0..15) and gdesc_bh[1] (16..19) */
	ctx->sbi.s_groups_count = 20;

	desc = ext4_get_group_desc(sb, 0, &bh);
	KUNIT_EXPECT_PTR_EQ(test, desc, &ctx->gdps[0]);
	KUNIT_EXPECT_PTR_EQ(test, bh, &ctx->gdesc_bh[0]);

	desc = ext4_get_group_desc(sb, 15, &bh);
	KUNIT_EXPECT_PTR_EQ(test, desc, &ctx->gdps[15]);
	KUNIT_EXPECT_PTR_EQ(test, bh, &ctx->gdesc_bh[0]);

	desc = ext4_get_group_desc(sb, 16, &bh);
	KUNIT_EXPECT_PTR_EQ(test, desc, &ctx->gdps[16]);
	KUNIT_EXPECT_PTR_EQ(test, bh, &ctx->gdesc_bh[1]);

	desc = ext4_get_group_desc(sb, 19, NULL);
	KUNIT_EXPECT_PTR_EQ(test, desc, &ctx->gdps[19]);

	/* Out-of-range block_group >= s_groups_count returns NULL */
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;
	KUNIT_EXPECT_NULL(test, ext4_get_group_desc(sb, 20, &bh));
	KUNIT_EXPECT_TRUE(test, !!(ctx->sbi.s_mount_state & EXT4_ERROR_FS));

	/* Missing group descriptor buffer_head slot returns NULL */
	ctx->gdesc_bh_ptrs[1] = NULL;
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;
	KUNIT_EXPECT_NULL(test, ext4_get_group_desc(sb, 16, &bh));
	KUNIT_EXPECT_TRUE(test, !!(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
}

static void test_get_group_info_lookup_and_null_guards(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	ctx->sbi.s_groups_count = 20;

	KUNIT_EXPECT_PTR_EQ(test, ext4_get_group_info(sb, 0),
			    ctx->grp_infos[0]);
	KUNIT_EXPECT_PTR_EQ(test, ext4_get_group_info(sb, 15),
			    ctx->grp_infos[15]);
	KUNIT_EXPECT_PTR_EQ(test, ext4_get_group_info(sb, 16),
			    ctx->grp_infos[16]);
	KUNIT_EXPECT_PTR_EQ(test, ext4_get_group_info(sb, 19),
			    ctx->grp_infos[19]);

	/* Out-of-bounds group returns NULL */
	KUNIT_EXPECT_NULL(test, ext4_get_group_info(sb, 20));

	/* NULL second-level slice returns NULL only for that slice */
	ctx->grp_info_slices[1] = NULL;
	KUNIT_EXPECT_PTR_EQ(test, ext4_get_group_info(sb, 15),
			    ctx->grp_infos[15]);
	KUNIT_EXPECT_NULL(test, ext4_get_group_info(sb, 16));

	/* NULL s_group_info table returns NULL */
	rcu_assign_pointer(ctx->sbi.s_group_info, NULL);
	KUNIT_EXPECT_NULL(test, ext4_get_group_info(sb, 0));
}

/* =========================================================================
 * 6. Block Bitmap Validation (CSUM, Metadata Self-Checks, Padding, Flags)
 * =========================================================================
 */

static void test_read_block_bitmap_validates_csum(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;
	s64 free_before;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* 1. Unverified buffer with valid CRC32C verifies and sets BH_Verified */
	clear_buffer_verified(&slot->bh);
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[1]));
	put_bh(bh);

#ifdef CONFIG_EXT4_DEBUG
	/* Simulated CRC failure triggers -EFSBADCRC and resets s_simulate_fail */
	clear_buffer_verified(&slot->bh);
	ctx->sbi.s_simulate_fail = EXT4_SIM_BBITMAP_CRC;
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSBADCRC);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_simulate_fail, 0UL);
	ctx->grp_infos[1]->bb_state = 0;
	percpu_counter_add(&ctx->sbi.s_freeclusters_counter,
			   ctx->grp_infos[1]->bb_free);
#endif

	/* 2. Corrupting free-space bit without updating csum -> -EFSBADCRC */
	free_before = percpu_counter_sum(&ctx->sbi.s_freeclusters_counter);
	clear_buffer_verified(&slot->bh);
	slot->cache_buf[40] ^= 0x01;

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_TRUE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSBADCRC);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[1]));
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_freeclusters_counter),
			free_before - ctx->grp_infos[1]->bb_free);

	/*
	 * 3. Subsequent read on already-corrupt group returns -EFSCORRUPTED
	 *    immediately without subtracting bb_free a second time.
	 */
	slot->cache_buf[40] ^= 0x01;
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_TRUE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_freeclusters_counter),
			free_before - ctx->grp_infos[1]->bb_free);
}

static void test_read_block_bitmap_validates_metadata_bits(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;
	static const unsigned int meta_bits[] = {
		0,	/* block bitmap in group 2 */
		1,	/* inode bitmap in group 2 */
		2,	/* first inode table block in group 2 */
		10,	/* middle inode table block in group 2 */
		17,	/* last inode table block in group 2 */
	};
	unsigned int i;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* Clearing any in-group metadata bit (with valid CRC) -> -EFSCORRUPTED */
	for (i = 0; i < ARRAY_SIZE(meta_bits); i++) {
		ctx->grp_infos[2]->bb_state = 0;
		clear_buffer_verified(&slot->bh);
		ext4_clear_bit(meta_bits[i], slot->cache_buf);
		balloc_test_sync_bbitmap_csum(ctx, 2, false);

		bh = ext4_read_block_bitmap(sb, 2);
		KUNIT_EXPECT_EQ_MSG(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED,
				    "cleared bit %u should fail validation",
				    meta_bits[i]);
		KUNIT_EXPECT_TRUE_MSG(test,
				      EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[2]),
				      "bit %u should mark BBITMAP_CORRUPT",
				      meta_bits[i]);

		ext4_set_bit(meta_bits[i], slot->cache_buf);
	}

	/* Out-of-group block bitmap (< start and >= end) without FLEX_BG */
	{
		struct balloc_test_blk *slot1, *slot3;

		slot1 = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
		slot3 = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(3));
		KUNIT_ASSERT_NOT_NULL(test, slot1);
		KUNIT_ASSERT_NOT_NULL(test, slot3);

		ctx->grp_infos[2]->bb_state = 0;
		clear_buffer_verified(&slot1->bh);
		ext4_block_bitmap_set(sb, &ctx->gdps[2],
				      balloc_test_bbitmap_blk(1));
		balloc_test_sync_bbitmap_csum(ctx, 2, false);
		bh = ext4_read_block_bitmap(sb, 2);
		KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

		ctx->grp_infos[2]->bb_state = 0;
		clear_buffer_verified(&slot3->bh);
		ext4_block_bitmap_set(sb, &ctx->gdps[2],
				      balloc_test_bbitmap_blk(3));
		balloc_test_sync_bbitmap_csum(ctx, 2, false);
		bh = ext4_read_block_bitmap(sb, 2);
		KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

		ext4_block_bitmap_set(sb, &ctx->gdps[2],
				      balloc_test_bbitmap_blk(2));
		balloc_test_sync_bbitmap_csum(ctx, 1, false);
		balloc_test_sync_bbitmap_csum(ctx, 3, false);
	}

	/* Out-of-group inode bitmap without FLEX_BG -> -EFSCORRUPTED */
	ctx->grp_infos[2]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 5);
	balloc_test_sync_bbitmap_csum(ctx, 2, false);
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], balloc_test_ibitmap_blk(2));

	/* Inode table before start or past end of group without FLEX_BG */
	ctx->grp_infos[2]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_inode_table_set(sb, &ctx->gdps[2], balloc_test_group_start(1) + 50);
	balloc_test_sync_bbitmap_csum(ctx, 2, false);
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

	ctx->grp_infos[2]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_inode_table_set(sb, &ctx->gdps[2],
			     balloc_test_group_start(2) +
			     BALLOC_TEST_BLOCKS_PER_GRP - 8);
	balloc_test_sync_bbitmap_csum(ctx, 2, false);
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

	/* With FLEX_BG enabled, out-of-group metadata skips per-group bit check */
	ext4_set_feature_flex_bg(sb);
	ctx->grp_infos[2]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_inode_bitmap_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 5);
	ext4_inode_table_set(sb, &ctx->gdps[2], balloc_test_group_start(0) + 20);
	ext4_clear_bit(1, slot->cache_buf);
	ext4_clear_bit(2, slot->cache_buf);
	balloc_test_sync_bbitmap_csum(ctx, 2, false);

	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	put_bh(bh);
}

static void test_read_block_bitmap_validates_padding(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(3));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* 1. Zero bit at first padding bit (1024) -> -EFSCORRUPTED */
	clear_buffer_verified(&slot->bh);
	ext4_clear_bit(1024, slot->cache_buf);
	balloc_test_sync_bbitmap_csum(ctx, 3, false);
	bh = ext4_read_block_bitmap(sb, 3);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[3]));
	ext4_set_bit(1024, slot->cache_buf);

	/* 2. Zero bit at very last bit of bitmap block (8191) -> -EFSCORRUPTED */
	ctx->grp_infos[3]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_clear_bit(8191, slot->cache_buf);
	balloc_test_sync_bbitmap_csum(ctx, 3, false);
	bh = ext4_read_block_bitmap(sb, 3);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
	ext4_set_bit(8191, slot->cache_buf);

	/* 3. Partial last group (300 blocks): unset bit 300 in padding fails */
	ctx->grp_infos[3]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_blocks_count_set(&ctx->es, balloc_test_group_start(3) + 300);
	balloc_test_sync_bbitmap_csum(ctx, 3, false);
	bh = ext4_read_block_bitmap(sb, 3);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

	/* Setting padding bits 300..1023 makes the partial last group valid */
	ctx->grp_infos[3]->bb_state = 0;
	clear_buffer_verified(&slot->bh);
	ext4_mark_bitmap_end(300, BALLOC_TEST_BLK_SIZE * 8, slot->cache_buf);
	balloc_test_sync_bbitmap_csum(ctx, 3, false);
	bh = ext4_read_block_bitmap(sb, 3);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	put_bh(bh);

	/* 4. Full 8192-cluster group (bitmap_size == offset) has no padding */
	ctx->sbi.s_blocks_per_group = 8192;
	ctx->sbi.s_clusters_per_group = 8192;
	ctx->es.s_blocks_per_group = cpu_to_le32(8192);
	ctx->es.s_clusters_per_group = cpu_to_le32(8192);
	ext4_blocks_count_set(&ctx->es, 1 + 4 * 8192);

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(0));
	KUNIT_ASSERT_NOT_NULL(test, slot);
	clear_buffer_verified(&slot->bh);
	ext4_clear_bit(8191, slot->cache_buf);
	balloc_test_sync_bbitmap_csum(ctx, 0, false);

	bh = ext4_read_block_bitmap(sb, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	put_bh(bh);
}

static void test_read_block_bitmap_validation_short_circuits(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/*
	 * 1. EXT4_FC_REPLAY skips csum and corrupt-flag checks without setting
	 *    BH_Verified.
	 */
	clear_buffer_verified(&slot->bh);
	slot->cache_buf[50] ^= 0xff;
	set_bit(EXT4_GROUP_INFO_BBITMAP_CORRUPT_BIT,
		&ctx->grp_infos[1]->bb_state);
	ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_FALSE(test, buffer_verified(bh));
	put_bh(bh);

	ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;
	slot->cache_buf[50] ^= 0xff;

	/* 2. Already buffer_verified(bh) short-circuits before checking grp */
	set_buffer_verified(&slot->bh);
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	put_bh(bh);

	/* 3. NULL ext4_group_info when !buffer_verified returns -EFSCORRUPTED */
	clear_buffer_verified(&slot->bh);
	ctx->grp_infos[1]->bb_state = 0;
	rcu_assign_pointer(ctx->sbi.s_group_info, NULL);
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
}

/* =========================================================================
 * 7. Block Bitmap Reading, Prefetching, and Lazy Initialization
 * =========================================================================
 */

static void test_read_block_bitmap_disk_and_cache_paths(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* 1. Cold read from disk populates cache, runs end_io, and validates */
	memset(slot->cache_buf, 0, sizeof(slot->cache_buf));
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	KUNIT_EXPECT_TRUE(test, ctx->last_read_op_flags ==
				(blk_opf_t)(REQ_META | REQ_PRIO));
	KUNIT_EXPECT_TRUE(test, buffer_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	KUNIT_EXPECT_FALSE(test, buffer_new(bh));
	put_bh(bh);

	/* 2. Warm read hits bitmap_uptodate(bh) without issuing I/O */
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	put_bh(bh);

	/*
	 * 3. Initialized group with BH_Uptodate set and BH_BITMAP_UPTODATE
	 *    clear promotes BH_BITMAP_UPTODATE and validates without disk I/O.
	 */
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);
	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));
	put_bh(bh);
}

static void test_read_block_bitmap_nowait_prefetch_locked(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;
	int ref_before;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/* 1. Locked buffer with ignore_locked=true returns NULL without leaking ref */
	ref_before = atomic_read(&slot->bh.b_count);
	lock_buffer(&slot->bh);
	bh = ext4_read_block_bitmap_nowait(sb, 2, true);
	unlock_buffer(&slot->bh);

	KUNIT_EXPECT_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), ref_before);
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 0U);

	/* 2. Uncached unlocked buffer with ignore_locked=true sets REQ_RAHEAD */
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);

	bh = ext4_read_block_bitmap_nowait(sb, 2, true);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	KUNIT_EXPECT_TRUE(test, ctx->last_read_op_flags ==
				(blk_opf_t)(REQ_META | REQ_PRIO | REQ_RAHEAD));
	KUNIT_EXPECT_EQ(test, ext4_wait_block_bitmap(sb, 2, bh), 0);
	put_bh(bh);
}

static void test_read_block_bitmap_uninit_group(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;
	unsigned int bit;

	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot);

	/*
	 * Mark group 1 EXT4_BG_BLOCK_UNINIT and poison cache_buf with 0xa5.
	 * Lazy init must zero the bitmap, set metadata bits 0..19, leave
	 * data bits 20..1023 free, set padding bits 1024..8191, and skip I/O.
	 */
	memset(slot->cache_buf, 0xa5, sizeof(slot->cache_buf));
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);
	ctx->gdps[1].bg_flags = cpu_to_le16(EXT4_BG_BLOCK_UNINIT);
	ext4_group_desc_csum_set(sb, 1, &ctx->gdps[1]);

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 0U);
	KUNIT_EXPECT_TRUE(test, buffer_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, (bool)bitmap_uptodate(bh));
	KUNIT_EXPECT_TRUE(test, buffer_verified(bh));

	for (bit = 0; bit < 20; bit++)
		KUNIT_EXPECT_TRUE_MSG(test,
				      (bool)ext4_test_bit(bit, bh->b_data),
				      "metadata bit %u must be set", bit);
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(20, bh->b_data));
	KUNIT_EXPECT_FALSE(test, (bool)ext4_test_bit(1023, bh->b_data));
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(1024, bh->b_data));
	KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(8191, bh->b_data));

	KUNIT_EXPECT_EQ(test, ext4_count_free(bh->b_data, 1024 / 8),
			ext4_free_clusters_after_init(sb, 1, &ctx->gdps[1]));
	put_bh(bh);

	/*
	 * Legacy GDT_CSUM (CRC16 without metadata_csum) with FLEX_BG external
	 * metadata on group 2 (0 base blocks, bitmaps & itable in group 3)
	 * initializes all 1024 data bits in group 2 to free without I/O.
	 */
	{
		struct balloc_test_blk *ext_slot;
		ext4_fsblk_t ext_bbitmap = balloc_test_group_start(3) + 50;

		ext4_clear_feature_metadata_csum(sb);
		ext4_set_feature_gdt_csum(sb);
		ext4_set_feature_flex_bg(sb);

		ext_slot = balloc_test_setup_blk(ctx, ext_bbitmap, false);
		KUNIT_ASSERT_NOT_NULL(test, ext_slot);
		memset(ext_slot->cache_buf, 0x5a, sizeof(ext_slot->cache_buf));

		ext4_block_bitmap_set(sb, &ctx->gdps[2], ext_bbitmap);
		ext4_inode_bitmap_set(sb, &ctx->gdps[2], ext_bbitmap + 1);
		ext4_inode_table_set(sb, &ctx->gdps[2], ext_bbitmap + 2);
		ctx->gdps[2].bg_flags = cpu_to_le16(EXT4_BG_BLOCK_UNINIT);
		ext4_group_desc_csum_set(sb, 2, &ctx->gdps[2]);

		bh = ext4_read_block_bitmap(sb, 2);
		KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
		KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 0U);
		KUNIT_EXPECT_EQ(test, ext4_count_free(bh->b_data, 1024 / 8),
				1024U);
		KUNIT_EXPECT_TRUE(test, (bool)ext4_test_bit(1024, bh->b_data));
		put_bh(bh);

		ext4_clear_feature_flex_bg(sb);
	}

	/*
	 * Without GDT checksums (neither metadata_csum nor gdt_csum),
	 * EXT4_BG_BLOCK_UNINIT is ignored and bitmap is read from disk.
	 */
	ext4_clear_feature_metadata_csum(sb);
	ext4_clear_feature_gdt_csum(sb);
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, ctx->read_nowait_calls, 1U);
	put_bh(bh);
}

static void test_read_block_bitmap_uninit_errors(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot0, *slot1;
	struct buffer_head *bh;
	s64 free_clu_before, free_ino_before;

	slot0 = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(0));
	slot1 = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(1));
	KUNIT_ASSERT_NOT_NULL(test, slot0);
	KUNIT_ASSERT_NOT_NULL(test, slot1);

	/* 1. Group 0 marked EXT4_BG_BLOCK_UNINIT is rejected -> -EFSCORRUPTED */
	clear_buffer_uptodate(&slot0->bh);
	clear_bitmap_uptodate(&slot0->bh);
	ctx->gdps[0].bg_flags = cpu_to_le16(EXT4_BG_BLOCK_UNINIT);
	ext4_group_desc_csum_set(sb, 0, &ctx->gdps[0]);

	bh = ext4_read_block_bitmap(sb, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

	/*
	 * 2. Group 1 marked EXT4_BG_BLOCK_UNINIT with bad GDT checksum fails
	 *    with -EFSBADCRC and marks BOTH block and inode bitmaps corrupt.
	 */
	free_clu_before = percpu_counter_sum(&ctx->sbi.s_freeclusters_counter);
	free_ino_before = percpu_counter_sum(&ctx->sbi.s_freeinodes_counter);
	clear_buffer_uptodate(&slot1->bh);
	clear_bitmap_uptodate(&slot1->bh);
	ctx->gdps[1].bg_flags = cpu_to_le16(EXT4_BG_BLOCK_UNINIT);
	ctx->gdps[1].bg_checksum = cpu_to_le16(0xdead);

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSBADCRC);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[1]));
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_IBITMAP_CORRUPT(ctx->grp_infos[1]));
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_freeclusters_counter),
			free_clu_before - ctx->grp_infos[1]->bb_free);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&ctx->sbi.s_freeinodes_counter),
			free_ino_before - BALLOC_TEST_INODES_PER_GRP);

	/*
	 * 3. Base metadata clusters exceeding bitmap block size ((bit_max >> 3)
	 *    >= bh->b_size) returns -EFSCORRUPTED instead of overflowing bh.
	 */
	ctx->grp_infos[1]->bb_state = 0;
	clear_buffer_uptodate(&slot1->bh);
	clear_bitmap_uptodate(&slot1->bh);
	ctx->sbi.s_gdb_count = BALLOC_TEST_BLK_SIZE * 8;
	ext4_group_desc_csum_set(sb, 1, &ctx->gdps[1]);

	bh = ext4_read_block_bitmap(sb, 1);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
}

static void test_read_and_wait_block_bitmap_errors(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot;
	struct buffer_head *bh;
	int ref_before;

	/* 1. Out-of-range block group -> -EFSCORRUPTED */
	bh = ext4_read_block_bitmap(sb, 99);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);

	/* 2. Block bitmap <= s_first_data_block (1) -> -EFSCORRUPTED + corrupt */
	ext4_block_bitmap_set(sb, &ctx->gdps[2], BALLOC_TEST_FIRST_DATA_BLK);
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[2]));

	/* 3. Block bitmap >= ext4_blocks_count(es) -> -EFSCORRUPTED + corrupt */
	ctx->grp_infos[2]->bb_state = 0;
	ext4_block_bitmap_set(sb, &ctx->gdps[2], BALLOC_TEST_TOTAL_BLKS);
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[2]));
	ext4_block_bitmap_set(sb, &ctx->gdps[2], balloc_test_bbitmap_blk(2));

	/* 4. ext4_sb_getblk allocation failure -> -ENOMEM without corrupt flag */
	ctx->grp_infos[2]->bb_state = 0;
	ctx->fail_getblk = true;
	bh = ext4_read_block_bitmap(sb, 2);
	ctx->fail_getblk = false;
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -ENOMEM);
	KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[2]));

	/* 5. Async read I/O failure -> -EIO, marks corrupt, releases bh ref */
	slot = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot);
	ref_before = atomic_read(&slot->bh.b_count);
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	clear_buffer_verified(&slot->bh);
	ctx->fail_read_eio = true;

	bh = ext4_read_block_bitmap(sb, 2);
	ctx->fail_read_eio = false;
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EIO);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_BBITMAP_CORRUPT(ctx->grp_infos[2]));
	KUNIT_EXPECT_EQ(test, atomic_read(&slot->bh.b_count), ref_before);

#ifdef CONFIG_EXT4_DEBUG
	ctx->grp_infos[2]->bb_state = 0;
	clear_buffer_uptodate(&slot->bh);
	clear_bitmap_uptodate(&slot->bh);
	ctx->sbi.s_simulate_fail = EXT4_SIM_BBITMAP_EIO;
	bh = ext4_read_block_bitmap(sb, 2);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(bh), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->sbi.s_simulate_fail, 0UL);
#endif

	/* 6. ext4_wait_block_bitmap guards on BH_New and group descriptor */
	clear_buffer_new(&slot->bh);
	KUNIT_EXPECT_EQ(test, ext4_wait_block_bitmap(sb, 99, &slot->bh), 0);
	set_buffer_new(&slot->bh);
	KUNIT_EXPECT_EQ(test, ext4_wait_block_bitmap(sb, 99, &slot->bh),
			-EFSCORRUPTED);
	clear_buffer_new(&slot->bh);
}

/* =========================================================================
 * 8. Free Cluster Reservation & Pool Accounting (ext4_claim_free_clusters)
 * =========================================================================
 */

static void balloc_test_set_cluster_pools(struct balloc_test_ctx *ctx,
					  s64 free_clu, s64 dirty_clu,
					  u64 r_blocks, s64 resv_clu)
{
	percpu_counter_set(&ctx->sbi.s_freeclusters_counter, free_clu);
	percpu_counter_set(&ctx->sbi.s_dirtyclusters_counter, dirty_clu);
	ctx->es.s_r_blocks_count_lo = cpu_to_le32(lower_32_bits(r_blocks));
	ctx->es.s_r_blocks_count_hi = cpu_to_le32(upper_32_bits(r_blocks));
	atomic64_set(&ctx->sbi.s_resv_clusters, resv_clu);
}

static void
test_claim_free_clusters_unprivileged_and_bigalloc(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;

	sbi->s_resuid = KUIDT_INIT(500);
	sbi->s_resgid = GLOBAL_ROOT_GID;
	balloc_test_override_creds(test, ctx, 1000, 1000, false, -1);

	/* Above-watermark fast path */
	balloc_test_set_cluster_pools(ctx, 100000, 10, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 25, 0), 0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_dirtyclusters_counter),
			35LL);

	/*
	 * Exact unprivileged limit:
	 * free=100, dirty=20, r_blocks=30, resv=15 -> usable = 100 - 20 - 45 = 35
	 */
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 36, 0), -ENOSPC);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_dirtyclusters_counter),
			20LL);

	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 35, 0), 0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_dirtyclusters_counter),
			55LL);

	/*
	 * Bigalloc (cluster_bits = 2 -> 4 blocks/cluster):
	 * r_blocks_count = 120 blocks -> 30 reserved clusters -> usable = 35.
	 */
	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;
	balloc_test_set_cluster_pools(ctx, 100, 20, 120, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 36, 0), -ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 35, 0), 0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_dirtyclusters_counter),
			55LL);

	balloc_test_restore_creds(ctx);
}

static void test_claim_free_clusters_root_and_reserved_pools(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct ext4_sb_info *sbi = &ctx->sbi;

	/*
	 * Pool layout: free=100, dirty=20, r_blocks=30, resv=15:
	 *  - Unprivileged limit:   35 clusters (100 - 20 - 30 - 15)
	 *  - Root-pool limit:      65 clusters (100 - 20 - 15; resv protected)
	 *  - Internal resv limit:  80 clusters (100 - 20; EXT4_MB_USE_RESERVED)
	 */
	sbi->s_resuid = KUIDT_INIT(500);
	sbi->s_resgid = GLOBAL_ROOT_GID;

	/* 1. Matching s_resuid grants root pool (65), still protects resv (66) */
	balloc_test_override_creds(test, ctx, 500, 1000, false, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 66, 0), -ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 65, 0), 0);

	/*
	 * 2. s_resgid == GLOBAL_ROOT_GID (0) does NOT grant root blocks via
	 *    group membership even when caller's fsgid is 0.
	 */
	balloc_test_override_creds(test, ctx, 1000, 0, false, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 36, 0), -ENOSPC);

#ifdef CONFIG_MULTIUSER
	/* 3. Non-root s_resgid grants root pool only when caller is in group */
	sbi->s_resgid = KGIDT_INIT(2000);
	balloc_test_override_creds(test, ctx, 1000, 1000, false, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 36, 0), -ENOSPC);

	balloc_test_override_creds(test, ctx, 1000, 1000, false, 2000);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 66, 0), -ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 65, 0), 0);
	sbi->s_resgid = GLOBAL_ROOT_GID;
#endif

	/* 4. EXT4_MB_USE_ROOT_BLOCKS grants root pool to unprivileged caller */
	balloc_test_override_creds(test, ctx, 1000, 1000, false, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test,
			ext4_claim_free_clusters(sbi, 66,
						 EXT4_MB_USE_ROOT_BLOCKS),
			-ENOSPC);
	KUNIT_EXPECT_EQ(test,
			ext4_claim_free_clusters(sbi, 65,
						 EXT4_MB_USE_ROOT_BLOCKS),
			0);

	/* 5. CAP_SYS_RESOURCE grants root pool regardless of uid/gid/flags */
	balloc_test_override_creds(test, ctx, 1000, 1000, true, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 66, 0), -ENOSPC);
	KUNIT_EXPECT_EQ(test, ext4_claim_free_clusters(sbi, 65, 0), 0);

	/* 6. EXT4_MB_USE_RESERVED dips into s_resv_clusters up to 80 clusters */
	balloc_test_override_creds(test, ctx, 1000, 1000, false, -1);
	balloc_test_set_cluster_pools(ctx, 100, 20, 30, 15);
	KUNIT_EXPECT_EQ(test,
			ext4_claim_free_clusters(sbi, 81, EXT4_MB_USE_RESERVED),
			-ENOSPC);
	KUNIT_EXPECT_EQ(test,
			ext4_claim_free_clusters(sbi, 80, EXT4_MB_USE_RESERVED),
			0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_dirtyclusters_counter),
			100LL);

	balloc_test_restore_creds(ctx);
}

/* =========================================================================
 * 9. Allocation Retry Policy (ext4_should_retry_alloc)
 * =========================================================================
 */

static void
test_should_retry_alloc_no_journal_and_retry_limit(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	int retries = 1;

	/* Without a journal, returns 0 without incrementing *retries */
	sbi->s_journal = NULL;
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 0);
	KUNIT_EXPECT_EQ(test, retries, 1);

	/* With journal and pending mb frees, retries 1..3 succeed, 4th stops */
	sbi->s_journal = &ctx->journal;
	atomic_set(&sbi->s_mb_free_pending, 4);
	retries = 0;

	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 1);
	KUNIT_EXPECT_EQ(test, retries, 1);
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 1);
	KUNIT_EXPECT_EQ(test, retries, 2);
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 1);
	KUNIT_EXPECT_EQ(test, retries, 3);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&sbi->s_sra_exceeded_retry_limit),
			0LL);

	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 0);
	KUNIT_EXPECT_EQ(test, retries, 4);
	KUNIT_EXPECT_EQ(test,
			percpu_counter_sum(&sbi->s_sra_exceeded_retry_limit),
			1LL);
}

static void
test_should_retry_alloc_free_space_and_discard_flush(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = &ctx->sbi;
	int retries;

	sbi->s_journal = &ctx->journal;
	atomic_set(&sbi->s_mb_free_pending, 0);
	clear_opt(sb, DISCARD);
	balloc_test_override_creds(test, ctx, 1000, 1000, false, -1);

	/* 1. No pending frees, no discard, 0 free clusters -> returns 0 */
	balloc_test_set_cluster_pools(ctx, 10, 10, 0, 0);
	retries = 0;
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 0);
	KUNIT_EXPECT_EQ(test, retries, 1);
	KUNIT_EXPECT_EQ(test, ctx->discard_work_runs, 0U);

	/* 2. Free cluster available -> returns 1 */
	balloc_test_set_cluster_pools(ctx, 11, 10, 0, 0);
	retries = 0;
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 1);

	/*
	 * 3. With DISCARD enabled, flushes s_discard_work while holding
	 *    s_retry_alloc_pending == 1 and rechecks free clusters afterward.
	 */
	set_opt(sb, DISCARD);
	balloc_test_set_cluster_pools(ctx, 10, 10, 0, 0);
	ctx->discard_release_clusters = 8;
	ctx->discard_wait_retry_pending = true;
	schedule_work(&sbi->s_discard_work);

	retries = 0;
	KUNIT_EXPECT_EQ(test, ext4_should_retry_alloc(sb, &retries), 1);
	ctx->discard_wait_retry_pending = false;
	KUNIT_EXPECT_EQ(test, ctx->discard_work_runs, 1U);
	KUNIT_EXPECT_EQ(test, ctx->discard_saw_retry_pending, 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&sbi->s_retry_alloc_pending), 0);

	balloc_test_restore_creds(ctx);
}

/* =========================================================================
 * 10. Metadata Block Allocation (ext4_new_meta_blocks)
 * =========================================================================
 */

static void test_new_meta_blocks_alloc_and_delalloc_quota(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct inode *inode = ctx->test_inode;
	unsigned long count;
	ext4_fsblk_t blk;
	int err = -EINVAL;

	/* 1. count == NULL defaults ar.len to 1; non-delalloc skips quota */
	ctx->mock_alloc_blk = 1234ULL;
	ctx->mock_alloc_len = 0;
	ctx->mock_alloc_err = 0;
	inode_set_bytes(inode, 0);

	blk = ext4_new_meta_blocks(NULL, inode, 500ULL, EXT4_MB_USE_ROOT_BLOCKS,
				   NULL, &err);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, blk, 1234ULL);
	KUNIT_EXPECT_PTR_EQ(test, ctx->last_ar.inode, inode);
	KUNIT_EXPECT_EQ(test, ctx->last_ar.goal, 500ULL);
	KUNIT_EXPECT_EQ(test, ctx->last_ar.len, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_ar.flags,
			(unsigned int)EXT4_MB_USE_ROOT_BLOCKS);
	KUNIT_EXPECT_EQ(test, inode_get_bytes(inode), 0LL);

	/*
	 * 2. Partial allocation (requested 8 clusters, granted 3) with
	 *    EXT4_MB_DELALLOC_RESERVED and bigalloc (4 blocks/cluster = 4096B
	 *    per cluster) updates *count to 3 and charges 12288 quota bytes.
	 */
	ctx->sbi.s_cluster_bits = 2;
	ctx->sbi.s_cluster_ratio = 4;
	ctx->mock_alloc_blk = 2400ULL;
	ctx->mock_alloc_len = 3;
	ctx->mock_alloc_err = 0;
	count = 8;

	blk = ext4_new_meta_blocks(NULL, inode, 2048ULL,
				   EXT4_MB_DELALLOC_RESERVED, &count, &err);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, blk, 2400ULL);
	KUNIT_EXPECT_EQ(test, count, 3UL);
	KUNIT_EXPECT_EQ(test, inode_get_bytes(inode), 12288LL);

	/* 3. Error from ext4_mb_new_blocks does not charge quota bytes */
	ctx->mock_alloc_err = -ENOSPC;
	count = 4;
	blk = ext4_new_meta_blocks(NULL, inode, 2048ULL,
				   EXT4_MB_DELALLOC_RESERVED, &count, &err);
	KUNIT_EXPECT_EQ(test, err, -ENOSPC);
	KUNIT_EXPECT_EQ(test, blk, 0ULL);
	KUNIT_EXPECT_EQ(test, inode_get_bytes(inode), 12288LL);
}

/* =========================================================================
 * 11. Free Cluster Summation (ext4_count_free_clusters)
 * =========================================================================
 */

static void
test_count_free_clusters_corrupt_and_64bit_groups(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct balloc_test_blk *slot2;

	slot2 = balloc_test_find_blk(ctx, balloc_test_bbitmap_blk(2));
	KUNIT_ASSERT_NOT_NULL(test, slot2);

	/* Default 4 groups: 1004 + 1004 + 1006 + 1004 = 4018 free clusters */
	KUNIT_EXPECT_EQ(test, ext4_count_free_clusters(sb), 4018ULL);

	/* Corrupt bitmap flag on group 2 excludes its 1006 clusters -> 3012 */
	clear_buffer_verified(&slot2->bh);
	set_bit(EXT4_GROUP_INFO_BBITMAP_CORRUPT_BIT,
		&ctx->grp_infos[2]->bb_state);
	KUNIT_EXPECT_EQ(test, ext4_count_free_clusters(sb), 3012ULL);

	/* NULL s_group_info still sums all valid group descriptors -> 4018 */
	set_buffer_verified(&slot2->bh);
	rcu_assign_pointer(ctx->sbi.s_group_info, NULL);
	KUNIT_EXPECT_EQ(test, ext4_count_free_clusters(sb), 4018ULL);
	rcu_assign_pointer(ctx->sbi.s_group_info, ctx->grp_info_slices);
	ctx->grp_infos[2]->bb_state = 0;

#ifndef EXT4FS_DEBUG
	/* 64-bit group descriptors with bg_free_blocks_count_hi > 0 */
	ctx->sbi.s_groups_count = 2;
	ext4_free_group_clusters_set(sb, &ctx->gdps[0], 0x10005U);
	ext4_free_group_clusters_set(sb, &ctx->gdps[1], 0x20003U);
	KUNIT_EXPECT_EQ(test, ext4_count_free_clusters(sb), 0x30008ULL);

	/* Unloaded group descriptor block is skipped cleanly */
	ctx->sbi.s_groups_count = 20;
	ctx->gdesc_bh_ptrs[1] = NULL;
	KUNIT_EXPECT_EQ(test, ext4_count_free_clusters(sb),
			0x30008ULL + 1006ULL + 1004ULL);
#endif
}

/* =========================================================================
 * 12. Inode Goal Block Selection (ext4_inode_to_goal_block)
 * =========================================================================
 */

static void test_inode_to_goal_block_flex_bg_and_delalloc(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode = ctx->test_inode;

	ctx->sbi.s_groups_count = 8;
	ext4_blocks_count_set(&ctx->es, 1 + 8 * BALLOC_TEST_BLOCKS_PER_GRP);
	set_opt(sb, DELALLOC);

	/* flex_size = 2 (< EXT4_FLEX_SIZE_DIR_ALLOC_SCHEME = 4): stays in bg 3 */
	ctx->sbi.s_log_groups_per_flex = 1;
	EXT4_I(inode)->i_block_group = 3;
	inode->i_mode = S_IFDIR | 0755;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(3));
	inode->i_mode = S_IFREG | 0644;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(3));

	/*
	 * flex_size = 4 (== EXT4_FLEX_SIZE_DIR_ALLOC_SCHEME):
	 *  - Directories and special/symlink inodes map to 1st group of flex_bg
	 *  - Regular files map to 2nd group of flex_bg (flex_start + 1)
	 */
	ctx->sbi.s_log_groups_per_flex = 2;

	EXT4_I(inode)->i_block_group = 3;
	inode->i_mode = S_IFDIR | 0755;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(0));
	inode->i_mode = S_IFLNK | 0777;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(0));
	inode->i_mode = S_IFREG | 0644;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(1));

	EXT4_I(inode)->i_block_group = 6;
	inode->i_mode = S_IFDIR | 0755;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(4));
	inode->i_mode = S_IFREG | 0644;
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode),
			balloc_test_group_start(5));
}

static void test_inode_to_goal_block_nodelalloc_colour(struct kunit *test)
{
	struct balloc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode = ctx->test_inode;
	ext4_fsblk_t g0_start = balloc_test_group_start(0);
	ext4_fsblk_t g3_start = balloc_test_group_start(3);
	ext4_fsblk_t goal_full, goal_partial;
	u32 rem;

	clear_opt(sb, DELALLOC);
	ctx->sbi.s_log_groups_per_flex = 0;
	inode->i_mode = S_IFREG | 0644;

	/*
	 * Full group 0 (1024 blocks -> step = 64) vs partial last group 3 with
	 * last_block - g3_start == 320 (321 blocks -> step = 20). Both calls
	 * execute in the same kthread, so (task_pid_nr(current) % 16) is fixed.
	 */
	ext4_blocks_count_set(&ctx->es, g3_start + 321);

	EXT4_I(inode)->i_block_group = 0;
	goal_full = ext4_inode_to_goal_block(inode);
	KUNIT_EXPECT_GE(test, goal_full, g0_start);
	KUNIT_EXPECT_LE(test, goal_full, g0_start + 15 * 64);
	div_u64_rem(goal_full - g0_start, 64, &rem);
	KUNIT_EXPECT_EQ(test, rem, 0U);

	EXT4_I(inode)->i_block_group = 3;
	goal_partial = ext4_inode_to_goal_block(inode);
	KUNIT_EXPECT_GE(test, goal_partial, g3_start);
	KUNIT_EXPECT_LE(test, goal_partial, g3_start + 15 * 20);
	div_u64_rem(goal_partial - g3_start, 20, &rem);
	KUNIT_EXPECT_EQ(test, rem, 0U);
	KUNIT_EXPECT_EQ(test, (goal_full - g0_start) * 20,
			(goal_partial - g3_start) * 64);

	/* Partial last group with < 16 blocks yields colour step 0 -> g3_start */
	ext4_blocks_count_set(&ctx->es, g3_start + 16);
	KUNIT_EXPECT_EQ(test, ext4_inode_to_goal_block(inode), g3_start);
}

static struct kunit_case balloc_test_cases[] = {
	KUNIT_CASE_PARAM(test_group_no_and_offset_matrix,
			 group_offset_gen_params),
	KUNIT_CASE(test_group_no_and_offset_optional_out_ptrs),
	KUNIT_CASE(test_bg_has_super_dense_all_groups),
	KUNIT_CASE(test_bg_has_super_sparse_super_powers),
	KUNIT_CASE(test_bg_has_super_sparse_super2_explicit_backups),
	KUNIT_CASE(test_bg_num_gdb_and_base_meta_non_metabg),
	KUNIT_CASE(test_bg_num_gdb_and_base_meta_hybrid_metabg),
	KUNIT_CASE(test_free_clusters_after_init_standard_and_partial_last),
	KUNIT_CASE(test_free_clusters_after_init_external_and_spanning_meta),
	KUNIT_CASE(test_free_clusters_after_init_bigalloc_cluster_sharing),
	KUNIT_CASE(test_get_group_desc_lookup_and_bounds),
	KUNIT_CASE(test_get_group_info_lookup_and_null_guards),
	KUNIT_CASE(test_read_block_bitmap_validates_csum),
	KUNIT_CASE(test_read_block_bitmap_validates_metadata_bits),
	KUNIT_CASE(test_read_block_bitmap_validates_padding),
	KUNIT_CASE(test_read_block_bitmap_validation_short_circuits),
	KUNIT_CASE(test_read_block_bitmap_disk_and_cache_paths),
	KUNIT_CASE(test_read_block_bitmap_nowait_prefetch_locked),
	KUNIT_CASE(test_read_block_bitmap_uninit_group),
	KUNIT_CASE(test_read_block_bitmap_uninit_errors),
	KUNIT_CASE(test_read_and_wait_block_bitmap_errors),
	KUNIT_CASE(test_claim_free_clusters_unprivileged_and_bigalloc),
	KUNIT_CASE(test_claim_free_clusters_root_and_reserved_pools),
	KUNIT_CASE(test_should_retry_alloc_no_journal_and_retry_limit),
	KUNIT_CASE(test_should_retry_alloc_free_space_and_discard_flush),
	KUNIT_CASE(test_new_meta_blocks_alloc_and_delalloc_quota),
	KUNIT_CASE(test_count_free_clusters_corrupt_and_64bit_groups),
	KUNIT_CASE(test_inode_to_goal_block_flex_bg_and_delalloc),
	KUNIT_CASE(test_inode_to_goal_block_nodelalloc_colour),
	{}
};

static struct kunit_suite balloc_test_suite = {
	.name = "ext4_balloc_test",
	.init = balloc_test_init,
	.test_cases = balloc_test_cases,
};

kunit_test_suites(&balloc_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 block allocation helpers in balloc.c");
MODULE_LICENSE("GPL");
