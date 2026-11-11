// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test of ext4 multiblocks allocation.
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/cpumask.h>
#include <linux/fs_context.h>
#include <linux/random.h>
#include <linux/sched.h>

#include "ext4.h"
#include "mballoc.h"

struct mbt_grp_ctx {
	struct buffer_head bitmap_bh;
	/* desc and gd_bh are just the place holders for now */
	struct ext4_group_desc desc;
	struct buffer_head gd_bh;
};

struct mbt_ctx {
	struct mbt_grp_ctx *grp_ctx;
};

struct mbt_ext4_super_block {
	struct ext4_super_block es;
	struct ext4_sb_info sbi;
	struct mbt_ctx mbt_ctx;
};

#define MBT_SB(_sb) (container_of((_sb)->s_fs_info, struct mbt_ext4_super_block, sbi))
#define MBT_CTX(_sb) (&MBT_SB(_sb)->mbt_ctx)
#define MBT_GRP_CTX(_sb, _group) (&MBT_CTX(_sb)->grp_ctx[_group])

static struct inode *mbt_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kmalloc_obj(struct ext4_inode_info);
	if (!ei)
		return NULL;

	INIT_LIST_HEAD(&ei->i_orphan);
	init_rwsem(&ei->xattr_sem);
	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	ext4_fc_init_inode(&ei->vfs_inode);

	return &ei->vfs_inode;
}

static void mbt_free_inode(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **mbt_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations mbt_sops = {
	.alloc_inode	= mbt_alloc_inode,
	.free_inode	= mbt_free_inode,
#ifdef CONFIG_QUOTA
	.get_dquots	= mbt_get_dquots,
#endif
};

static int mbt_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type mbt_fs_type = {
	.name			= "mballoc test",
	.init_fs_context	= mbt_init_fs_context,
	.kill_sb		= kill_anon_super,
};

static int mbt_mb_init(struct super_block *sb)
{
	ext4_fsblk_t block;
	int ret;

	/* needed by ext4_mb_init->bdev_rot(sb->s_bdev) */
	sb->s_bdev = kzalloc_obj(*sb->s_bdev);
	if (sb->s_bdev == NULL)
		return -ENOMEM;
	bdev_set_flag(sb->s_bdev, BD_READ_ONLY);

	sb->s_bdev->bd_queue = kzalloc_obj(struct request_queue);
	if (sb->s_bdev->bd_queue == NULL) {
		kfree(sb->s_bdev);
		return -ENOMEM;
	}

	/*
	 * needed by ext4_mb_init->ext4_mb_init_backend-> sbi->s_buddy_cache =
	 * new_inode(sb);
	 */
	INIT_LIST_HEAD(&sb->s_inodes);
	sb->s_op = &mbt_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif

	ret = ext4_mb_init(sb);
	if (ret != 0)
		goto err_out;

	block = ext4_count_free_clusters(sb);
	ret = percpu_counter_init(&EXT4_SB(sb)->s_freeclusters_counter, block,
				  GFP_KERNEL);
	if (ret != 0)
		goto err_mb_release;

	ret = percpu_counter_init(&EXT4_SB(sb)->s_dirtyclusters_counter, 0,
				  GFP_KERNEL);
	if (ret != 0)
		goto err_freeclusters;

	return 0;

err_freeclusters:
	percpu_counter_destroy(&EXT4_SB(sb)->s_freeclusters_counter);
err_mb_release:
	ext4_mb_release(sb);
err_out:
	kfree(sb->s_bdev->bd_queue);
	kfree(sb->s_bdev);
	return ret;
}

static void mbt_mb_release(struct super_block *sb)
{
	percpu_counter_destroy(&EXT4_SB(sb)->s_dirtyclusters_counter);
	percpu_counter_destroy(&EXT4_SB(sb)->s_freeclusters_counter);
	ext4_mb_release(sb);
	kfree(sb->s_bdev->bd_queue);
	kfree(sb->s_bdev);
}

static int mbt_set(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static struct super_block *mbt_ext4_alloc_super_block(void)
{
	struct mbt_ext4_super_block *fsb;
	struct super_block *sb;
	struct fs_context *fc;
	struct ext4_sb_info *sbi;

	fsb = kzalloc_obj(*fsb);
	if (fsb == NULL)
		return NULL;

	fc = fs_context_for_mount(&mbt_fs_type, 0);
	if (IS_ERR(fc))
		goto out;

	sb = sget_fc(fc, NULL, mbt_set);
	put_fs_context(fc);
	if (IS_ERR(sb))
		goto out;

	sbi = &fsb->sbi;

	sbi->s_blockgroup_lock =
		kzalloc_obj(struct blockgroup_lock);
	if (!sbi->s_blockgroup_lock)
		goto out_deactivate;

	bgl_lock_init(sbi->s_blockgroup_lock);
	spin_lock_init(&sbi->s_error_lock);
	ratelimit_state_init(&sbi->s_err_ratelimit_state, 5 * HZ, 10);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state, 5 * HZ, 10);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state, 5 * HZ, 10);

	sbi->s_es = &fsb->es;
	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	set_opt(sb, ERRORS_CONT);

	up_write(&sb->s_umount);
	return sb;

out_deactivate:
	deactivate_locked_super(sb);
out:
	kfree(fsb);
	return NULL;
}

static void mbt_ext4_free_super_block(struct super_block *sb)
{
	struct mbt_ext4_super_block *fsb = MBT_SB(sb);
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	kfree(sbi->s_blockgroup_lock);
	deactivate_super(sb);
	kfree(fsb);
}

struct mbt_ext4_block_layout {
	unsigned char blocksize_bits;
	unsigned int cluster_bits;
	uint32_t blocks_per_group;
	ext4_group_t group_count;
	uint16_t desc_size;
};

static void mbt_init_sb_layout(struct super_block *sb,
			       struct mbt_ext4_block_layout *layout)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;

	sb->s_blocksize = 1UL << layout->blocksize_bits;
	sb->s_blocksize_bits = layout->blocksize_bits;

	sbi->s_groups_count = layout->group_count;
	sbi->s_blocks_per_group = layout->blocks_per_group;
	sbi->s_cluster_bits = layout->cluster_bits;
	sbi->s_cluster_ratio = 1U << layout->cluster_bits;
	sbi->s_clusters_per_group = layout->blocks_per_group >>
				    layout->cluster_bits;
	sbi->s_desc_size = layout->desc_size;
	sbi->s_desc_per_block_bits =
		sb->s_blocksize_bits - (fls(layout->desc_size) - 1);
	sbi->s_desc_per_block = 1 << sbi->s_desc_per_block_bits;

	es->s_first_data_block = cpu_to_le32(0);
	es->s_blocks_count_lo = cpu_to_le32(layout->blocks_per_group *
					    layout->group_count);
}

static int mbt_grp_ctx_init(struct super_block *sb,
			    struct mbt_grp_ctx *grp_ctx)
{
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);

	grp_ctx->bitmap_bh.b_data = kzalloc(EXT4_BLOCK_SIZE(sb), GFP_KERNEL);
	if (grp_ctx->bitmap_bh.b_data == NULL)
		return -ENOMEM;
	mb_set_bits(grp_ctx->bitmap_bh.b_data, max, sb->s_blocksize * 8 - max);
	ext4_free_group_clusters_set(sb, &grp_ctx->desc, max);

	return 0;
}

static void mbt_grp_ctx_release(struct mbt_grp_ctx *grp_ctx)
{
	kfree(grp_ctx->bitmap_bh.b_data);
	grp_ctx->bitmap_bh.b_data = NULL;
}

static void mbt_ctx_mark_used(struct super_block *sb, ext4_group_t group,
			      unsigned int start, unsigned int len)
{
	struct mbt_grp_ctx *grp_ctx = MBT_GRP_CTX(sb, group);

	mb_set_bits(grp_ctx->bitmap_bh.b_data, start, len);
}

static void *mbt_ctx_bitmap(struct super_block *sb, ext4_group_t group)
{
	struct mbt_grp_ctx *grp_ctx = MBT_GRP_CTX(sb, group);

	return grp_ctx->bitmap_bh.b_data;
}

/* called after mbt_init_sb_layout */
static int mbt_ctx_init(struct super_block *sb)
{
	struct mbt_ctx *ctx = MBT_CTX(sb);
	ext4_group_t i, ngroups = ext4_get_groups_count(sb);

	ctx->grp_ctx = kzalloc_objs(struct mbt_grp_ctx, ngroups);
	if (ctx->grp_ctx == NULL)
		return -ENOMEM;

	for (i = 0; i < ngroups; i++)
		if (mbt_grp_ctx_init(sb, &ctx->grp_ctx[i]))
			goto out;

	/*
	 * first data block(first cluster in first group) is used by
	 * metadata, mark it used to avoid to alloc data block at first
	 * block which will fail ext4_sb_block_valid check.
	 */
	mb_set_bits(ctx->grp_ctx[0].bitmap_bh.b_data, 0, 1);
	ext4_free_group_clusters_set(sb, &ctx->grp_ctx[0].desc,
				     EXT4_CLUSTERS_PER_GROUP(sb) - 1);

	return 0;
out:
	while (i-- > 0)
		mbt_grp_ctx_release(&ctx->grp_ctx[i]);
	kfree(ctx->grp_ctx);
	return -ENOMEM;
}

static void mbt_ctx_release(struct super_block *sb)
{
	struct mbt_ctx *ctx = MBT_CTX(sb);
	ext4_group_t i, ngroups = ext4_get_groups_count(sb);

	for (i = 0; i < ngroups; i++)
		mbt_grp_ctx_release(&ctx->grp_ctx[i]);
	kfree(ctx->grp_ctx);
}

static struct buffer_head *
ext4_read_block_bitmap_nowait_stub(struct super_block *sb, ext4_group_t block_group,
				   bool ignore_locked)
{
	struct mbt_grp_ctx *grp_ctx = MBT_GRP_CTX(sb, block_group);

	/* paired with brelse from caller of ext4_read_block_bitmap_nowait */
	get_bh(&grp_ctx->bitmap_bh);
	return &grp_ctx->bitmap_bh;
}

static int ext4_wait_block_bitmap_stub(struct super_block *sb,
				       ext4_group_t block_group,
				       struct buffer_head *bh)
{
	/*
	 * real ext4_wait_block_bitmap will set these flags and
	 * functions like ext4_mb_init_cache will verify the flags.
	 */
	set_buffer_uptodate(bh);
	set_bitmap_uptodate(bh);
	set_buffer_verified(bh);
	return 0;
}

static struct ext4_group_desc *
ext4_get_group_desc_stub(struct super_block *sb, ext4_group_t block_group,
			 struct buffer_head **bh)
{
	struct mbt_grp_ctx *grp_ctx = MBT_GRP_CTX(sb, block_group);

	if (bh != NULL)
		*bh = &grp_ctx->gd_bh;

	return &grp_ctx->desc;
}

static int
ext4_mb_mark_context_stub(handle_t *handle, struct super_block *sb, bool state,
			  ext4_group_t group, ext4_grpblk_t blkoff,
			  ext4_grpblk_t len, int flags,
			  ext4_grpblk_t *ret_changed)
{
	struct mbt_grp_ctx *grp_ctx = MBT_GRP_CTX(sb, group);
	struct buffer_head *bitmap_bh = &grp_ctx->bitmap_bh;
	struct ext4_group_desc *gdp = &grp_ctx->desc;
	ext4_grpblk_t i, already = 0, changed = len;
	__u32 free;

	for (i = 0; i < len; i++) {
		if (mb_test_bit_test(blkoff + i, bitmap_bh->b_data) == state)
			already++;
	}
	if (flags & 0x0001 /* EXT4_MB_BITMAP_MARKED_CHECK */)
		changed = len - already;

	free = ext4_free_group_clusters(sb, gdp);
	if (state) {
		mb_set_bits(bitmap_bh->b_data, blkoff, len);
		if (free >= changed)
			ext4_free_group_clusters_set(sb, gdp, free - changed);
	} else {
		mb_clear_bits_test(bitmap_bh->b_data, blkoff, len);
		ext4_free_group_clusters_set(sb, gdp, free + changed);
	}

	if (ret_changed)
		*ret_changed = changed;

	return 0;
}

static int
ext4_issue_discard_stub(struct super_block *sb, ext4_group_t block_group,
			ext4_grpblk_t cluster, int count)
{
	return 0;
}

static struct inode *mbt_alloc_test_inode(struct kunit *test,
					  struct super_block *sb,
					  umode_t mode)
{
	struct ext4_inode_info *ei;
	struct inode *inode;

	ei = kunit_kzalloc(test, sizeof(*ei), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ei);

	INIT_LIST_HEAD(&ei->i_orphan);
	init_rwsem(&ei->xattr_sem);
	init_rwsem(&ei->i_data_sem);
	ei->i_prealloc_node = RB_ROOT;
	atomic_set(&ei->i_prealloc_active, 0);
	rwlock_init(&ei->i_prealloc_lock);
	spin_lock_init(&ei->i_block_reservation_lock);
	spin_lock_init(&ei->i_raw_lock);

	inode = &ei->vfs_inode;
	inode_init_once(inode);
	spin_lock_init(&inode->i_lock);
	init_rwsem(&inode->i_rwsem);
	init_rwsem(&inode->i_data.invalidate_lock);
	ext4_fc_init_inode(inode);
	inode->i_sb = sb;
	inode->i_ino = 12;
	inode->i_mode = mode;
	inode->i_flags |= S_NOQUOTA;
	atomic_set(&inode->i_writecount, 1);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);

	return inode;
}

#define TEST_GOAL_GROUP 1
static int mbt_kunit_init(struct kunit *test)
{
	struct mbt_ext4_block_layout *layout =
		(struct mbt_ext4_block_layout *)(test->param_value);
	struct super_block *sb;
	int ret;

	sb = mbt_ext4_alloc_super_block();
	if (sb == NULL)
		return -ENOMEM;

	mbt_init_sb_layout(sb, layout);

	ret = mbt_ctx_init(sb);
	if (ret != 0) {
		mbt_ext4_free_super_block(sb);
		return ret;
	}

	kunit_activate_static_stub(test,
				   ext4_read_block_bitmap_nowait,
				   ext4_read_block_bitmap_nowait_stub);
	kunit_activate_static_stub(test,
				   ext4_wait_block_bitmap,
				   ext4_wait_block_bitmap_stub);
	kunit_activate_static_stub(test,
				   ext4_get_group_desc,
				   ext4_get_group_desc_stub);
	kunit_activate_static_stub(test,
				   ext4_mb_mark_context,
				   ext4_mb_mark_context_stub);
	kunit_activate_static_stub(test,
				   ext4_issue_discard,
				   ext4_issue_discard_stub);

	/* stub function will be called in mbt_mb_init->ext4_mb_init */
	if (mbt_mb_init(sb) != 0) {
		mbt_ctx_release(sb);
		mbt_ext4_free_super_block(sb);
		return -ENOMEM;
	}

	test->priv = sb;

	return 0;
}

static void mbt_kunit_exit(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;

	if (!sb)
		return;

	if (sb->s_blocksize <= PAGE_SIZE)
		ext4_mb_discard_preallocations_test(sb, 0);
	mbt_mb_release(sb);
	rcu_barrier();
	mbt_ctx_release(sb);
	mbt_ext4_free_super_block(sb);
}

static void test_new_blocks_simple(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct inode *inode;
	struct ext4_allocation_request ar;
	ext4_group_t i, goal_group = TEST_GOAL_GROUP;
	int err = 0;
	ext4_fsblk_t found;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);
	ar.inode = inode;

	/* get block at goal */
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple_test(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test, ar.goal, found,
		"failed to alloc block at goal, expected %llu found %llu",
		ar.goal, found);

	/* get block after goal in goal group */
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple_test(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test, ar.goal + EXT4_C2B(sbi, 1), found,
		"failed to alloc block after goal in goal group, expected %llu found %llu",
		ar.goal + 1, found);

	/* get block after goal group */
	mbt_ctx_mark_used(sb, goal_group, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple_test(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test,
		ext4_group_first_block_no(sb, goal_group + 1), found,
		"failed to alloc block after goal group, expected %llu found %llu",
		ext4_group_first_block_no(sb, goal_group + 1), found);

	/* get block before goal group */
	for (i = goal_group; i < ext4_get_groups_count(sb); i++)
		mbt_ctx_mark_used(sb, i, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple_test(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test,
		ext4_group_first_block_no(sb, 0) + EXT4_C2B(sbi, 1), found,
		"failed to alloc block before goal group, expected %llu found %llu",
		ext4_group_first_block_no(sb, 0 + EXT4_C2B(sbi, 1)), found);

	/* no block available, fail to allocate block */
	for (i = 0; i < ext4_get_groups_count(sb); i++)
		mbt_ctx_mark_used(sb, i, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple_test(&ar, &err);
	KUNIT_ASSERT_NE_MSG(test, err, 0,
		"unexpectedly get block when no block is available");
}

#define TEST_RANGE_COUNT 8

struct test_range {
	ext4_grpblk_t start;
	ext4_grpblk_t len;
};

static void
mbt_generate_test_ranges(struct super_block *sb, struct test_range *ranges,
			 int count)
{
	ext4_grpblk_t start, len, max;
	int i;

	max = EXT4_CLUSTERS_PER_GROUP(sb) / count;
	for (i = 0; i < count; i++) {
		start = get_random_u32() % max;
		len = get_random_u32() % max;
		len = min(len, max - start);

		ranges[i].start = start + i * max;
		ranges[i].len = len;
	}
}

static void
validate_free_blocks_simple(struct kunit *test, struct super_block *sb,
			    ext4_group_t goal_group, ext4_grpblk_t start,
			    ext4_grpblk_t len)
{
	void *bitmap;
	ext4_grpblk_t bit, max = EXT4_CLUSTERS_PER_GROUP(sb);
	ext4_group_t i;

	for (i = 0; i < ext4_get_groups_count(sb); i++) {
		if (i == goal_group)
			continue;

		bitmap = mbt_ctx_bitmap(sb, i);
		bit = mb_find_next_zero_bit_test(bitmap, max, 0);
		KUNIT_ASSERT_EQ_MSG(test, bit, max,
				    "free block on unexpected group %d", i);
	}

	bitmap = mbt_ctx_bitmap(sb, goal_group);
	bit = mb_find_next_zero_bit_test(bitmap, max, 0);
	KUNIT_ASSERT_EQ(test, bit, start);

	bit = mb_find_next_bit_test(bitmap, max, bit + 1);
	KUNIT_ASSERT_EQ(test, bit, start + len);
}

static void
test_free_blocks_simple_range(struct kunit *test, ext4_group_t goal_group,
			      ext4_grpblk_t start, ext4_grpblk_t len)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	ext4_fsblk_t block;

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	if (len == 0)
		return;

	block = ext4_group_first_block_no(sb, goal_group) +
		EXT4_C2B(sbi, start);
	ext4_free_blocks_simple_test(inode, block, len);
	validate_free_blocks_simple(test, sb, goal_group, start, len);
	mbt_ctx_mark_used(sb, goal_group, 0, EXT4_CLUSTERS_PER_GROUP(sb));
}

static void test_free_blocks_simple(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	ext4_group_t i;
	struct test_range ranges[TEST_RANGE_COUNT];

	for (i = 0; i < ext4_get_groups_count(sb); i++)
		mbt_ctx_mark_used(sb, i, 0, max);

	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_free_blocks_simple_range(test, TEST_GOAL_GROUP,
			ranges[i].start, ranges[i].len);
}

static void
test_mark_diskspace_used_range(struct kunit *test,
			       struct ext4_allocation_context *ac,
			       ext4_grpblk_t start,
			       ext4_grpblk_t len)
{
	struct super_block *sb = (struct super_block *)test->priv;
	int ret;
	void *bitmap;
	ext4_grpblk_t i, max;

	/* ext4_mb_mark_diskspace_used will BUG if len is 0 */
	if (len == 0)
		return;

	ac->ac_b_ex.fe_group = TEST_GOAL_GROUP;
	ac->ac_b_ex.fe_start = start;
	ac->ac_b_ex.fe_len = len;

	bitmap = mbt_ctx_bitmap(sb, TEST_GOAL_GROUP);
	memset(bitmap, 0, sb->s_blocksize);
	ret = ext4_mb_mark_diskspace_used_test(ac, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	max = EXT4_CLUSTERS_PER_GROUP(sb);
	i = mb_find_next_bit_test(bitmap, max, 0);
	KUNIT_ASSERT_EQ(test, i, start);
	i = mb_find_next_zero_bit_test(bitmap, max, i + 1);
	KUNIT_ASSERT_EQ(test, i, start + len);
	i = mb_find_next_bit_test(bitmap, max, i + 1);
	KUNIT_ASSERT_EQ(test, max, i);
}

static void test_mark_diskspace_used(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct inode *inode;
	struct ext4_allocation_context ac;
	struct test_range ranges[TEST_RANGE_COUNT];
	int i;

	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	ac.ac_status = AC_STATUS_FOUND;
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_mark_diskspace_used_range(test, &ac, ranges[i].start,
					       ranges[i].len);
}

static void mbt_generate_buddy(struct super_block *sb, void *buddy,
			       void *bitmap, struct ext4_group_info *grp)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	uint32_t order, off;
	void *bb, *bb_h;
	int max;

	memset(buddy, 0xff, sb->s_blocksize);
	memset(grp, 0, offsetof(struct ext4_group_info,
				 bb_counters[MB_NUM_ORDERS(sb)]));

	bb = bitmap;
	max = EXT4_CLUSTERS_PER_GROUP(sb);
	bb_h = buddy + sbi->s_mb_offsets[1];

	off = mb_find_next_zero_bit_test(bb, max, 0);
	grp->bb_first_free = off;
	while (off < max) {
		grp->bb_counters[0]++;
		grp->bb_free++;

		if (!(off & 1) && !mb_test_bit_test(off + 1, bb)) {
			grp->bb_free++;
			grp->bb_counters[0]--;
			mb_clear_bit_test(off >> 1, bb_h);
			grp->bb_counters[1]++;
			grp->bb_largest_free_order = 1;
			off++;
		}

		off = mb_find_next_zero_bit_test(bb, max, off + 1);
	}

	for (order = 1; order < MB_NUM_ORDERS(sb) - 1; order++) {
		bb = buddy + sbi->s_mb_offsets[order];
		bb_h = buddy + sbi->s_mb_offsets[order + 1];
		max = max >> 1;
		off = mb_find_next_zero_bit_test(bb, max, 0);

		while (off < max) {
			if (!(off & 1) && !mb_test_bit_test(off + 1, bb)) {
				mb_set_bits(bb, off, 2);
				grp->bb_counters[order] -= 2;
				mb_clear_bit_test(off >> 1, bb_h);
				grp->bb_counters[order + 1]++;
				grp->bb_largest_free_order = order + 1;
				off++;
			}

			off = mb_find_next_zero_bit_test(bb, max, off + 1);
		}
	}

	max = EXT4_CLUSTERS_PER_GROUP(sb);
	off = mb_find_next_zero_bit_test(bitmap, max, 0);
	while (off < max) {
		grp->bb_fragments++;

		off = mb_find_next_bit_test(bitmap, max, off + 1);
		if (off + 1 >= max)
			break;

		off = mb_find_next_zero_bit_test(bitmap, max, off + 1);
	}
}

static void
mbt_validate_group_info(struct kunit *test, struct ext4_group_info *grp1,
			struct ext4_group_info *grp2)
{
	struct super_block *sb = (struct super_block *)test->priv;
	int i;

	KUNIT_ASSERT_EQ(test, grp1->bb_first_free,
			grp2->bb_first_free);
	KUNIT_ASSERT_EQ(test, grp1->bb_fragments,
			grp2->bb_fragments);
	KUNIT_ASSERT_EQ(test, grp1->bb_free, grp2->bb_free);
	KUNIT_ASSERT_EQ(test, grp1->bb_largest_free_order,
			grp2->bb_largest_free_order);

	for (i = 1; i < MB_NUM_ORDERS(sb); i++) {
		KUNIT_ASSERT_EQ_MSG(test, grp1->bb_counters[i],
				    grp2->bb_counters[i],
				    "bb_counters[%d] diffs, expected %d, generated %d",
				    i, grp1->bb_counters[i],
				    grp2->bb_counters[i]);
	}
}

static void
do_test_generate_buddy(struct kunit *test, struct super_block *sb, void *bitmap,
			   void *mbt_buddy, struct ext4_group_info *mbt_grp,
			   void *ext4_buddy, struct ext4_group_info *ext4_grp)
{
	int i;

	mbt_generate_buddy(sb, mbt_buddy, bitmap, mbt_grp);

	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		ext4_grp->bb_counters[i] = 0;
	/* needed by validation in ext4_mb_generate_buddy */
	ext4_grp->bb_free = mbt_grp->bb_free;
	memset(ext4_buddy, 0xff, sb->s_blocksize);
	ext4_mb_generate_buddy_test(sb, ext4_buddy, bitmap, TEST_GOAL_GROUP,
			       ext4_grp);

	KUNIT_ASSERT_MEMEQ(test, mbt_buddy, ext4_buddy, sb->s_blocksize);
	mbt_validate_group_info(test, mbt_grp, ext4_grp);
}

static void test_mb_generate_buddy(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	void *bitmap, *expected_bb, *generate_bb;
	struct ext4_group_info *expected_grp, *generate_grp;
	struct test_range ranges[TEST_RANGE_COUNT];
	int i;

	bitmap = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bitmap);
	expected_bb = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_bb);
	generate_bb = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, generate_bb);
	expected_grp = kunit_kzalloc(test, offsetof(struct ext4_group_info,
				bb_counters[MB_NUM_ORDERS(sb)]), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_grp);
	generate_grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	KUNIT_ASSERT_NOT_NULL(test, generate_grp);

	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++) {
		mb_set_bits(bitmap, ranges[i].start, ranges[i].len);
		do_test_generate_buddy(test, sb, bitmap, expected_bb,
				       expected_grp, generate_bb, generate_grp);
	}
}

static void
test_mb_mark_used_range(struct kunit *test, struct ext4_buddy *e4b,
			ext4_grpblk_t start, ext4_grpblk_t len, void *bitmap,
			void *buddy, struct ext4_group_info *grp)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_free_extent ex;
	int i;

	/* mb_mark_used only accepts non-zero len */
	if (len == 0)
		return;

	ex.fe_start = start;
	ex.fe_len = len;
	ex.fe_group = TEST_GOAL_GROUP;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	mb_mark_used_test(e4b, &ex);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	mb_set_bits(bitmap, start, len);
	/* bypass bb_free validatoin in ext4_mb_generate_buddy */
	grp->bb_free -= len;
	memset(buddy, 0xff, sb->s_blocksize);
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	ext4_mb_generate_buddy_test(sb, buddy, bitmap, 0, grp);

	KUNIT_ASSERT_MEMEQ(test, buddy, e4b->bd_buddy, sb->s_blocksize);
	mbt_validate_group_info(test, grp, e4b->bd_info);
}

static void test_mb_mark_used(struct kunit *test)
{
	struct ext4_buddy e4b;
	struct super_block *sb = (struct super_block *)test->priv;
	void *bitmap, *buddy;
	struct ext4_group_info *grp;
	int ret;
	struct test_range ranges[TEST_RANGE_COUNT];
	int i;

	/* buddy cache assumes that each page contains at least one block */
	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	bitmap = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bitmap);
	buddy = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buddy);
	grp = kunit_kzalloc(test, offsetof(struct ext4_group_info,
				bb_counters[MB_NUM_ORDERS(sb)]), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, grp);

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	grp->bb_free = EXT4_CLUSTERS_PER_GROUP(sb);
	grp->bb_largest_free_order = -1;
	grp->bb_avg_fragment_size_order = -1;
	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_mb_mark_used_range(test, &e4b, ranges[i].start,
					ranges[i].len, bitmap, buddy, grp);

	ext4_mb_unload_buddy_test(&e4b);
}

static void
test_mb_free_blocks_range(struct kunit *test, struct ext4_buddy *e4b,
			  ext4_grpblk_t start, ext4_grpblk_t len, void *bitmap,
			  void *buddy, struct ext4_group_info *grp)
{
	struct super_block *sb = (struct super_block *)test->priv;
	int i;

	/* mb_free_blocks will WARN if len is 0 */
	if (len == 0)
		return;

	ext4_lock_group(sb, e4b->bd_group);
	mb_free_blocks_test(NULL, e4b, start, len);
	ext4_unlock_group(sb, e4b->bd_group);

	mb_clear_bits_test(bitmap, start, len);
	/* bypass bb_free validatoin in ext4_mb_generate_buddy */
	grp->bb_free += len;
	memset(buddy, 0xff, sb->s_blocksize);
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	ext4_mb_generate_buddy_test(sb, buddy, bitmap, 0, grp);

	KUNIT_ASSERT_MEMEQ(test, buddy, e4b->bd_buddy, sb->s_blocksize);
	mbt_validate_group_info(test, grp, e4b->bd_info);

}

static void test_mb_free_blocks(struct kunit *test)
{
	struct ext4_buddy e4b;
	struct super_block *sb = (struct super_block *)test->priv;
	void *bitmap, *buddy;
	struct ext4_group_info *grp;
	struct ext4_free_extent ex;
	int ret;
	int i;
	struct test_range ranges[TEST_RANGE_COUNT];

	/* buddy cache assumes that each page contains at least one block */
	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	bitmap = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bitmap);
	buddy = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buddy);
	grp = kunit_kzalloc(test, offsetof(struct ext4_group_info,
				bb_counters[MB_NUM_ORDERS(sb)]), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, grp);

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ex.fe_start = 0;
	ex.fe_len = EXT4_CLUSTERS_PER_GROUP(sb);
	ex.fe_group = TEST_GOAL_GROUP;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	mb_mark_used_test(&e4b, &ex);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	grp->bb_free = 0;
	grp->bb_largest_free_order = -1;
	grp->bb_avg_fragment_size_order = -1;
	memset(bitmap, 0xff, sb->s_blocksize);

	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_mb_free_blocks_range(test, &e4b, ranges[i].start,
					  ranges[i].len, bitmap, buddy, grp);

	ext4_mb_unload_buddy_test(&e4b);
}

#define COUNT_FOR_ESTIMATE 100000
static void test_mb_mark_used_cost(struct kunit *test)
{
	struct ext4_buddy e4b;
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_free_extent ex;
	int ret;
	struct test_range ranges[TEST_RANGE_COUNT];
	int i, j;
	unsigned long start, end, all = 0;

	/* buddy cache assumes that each page contains at least one block */
	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ex.fe_group = TEST_GOAL_GROUP;
	for (j = 0; j < COUNT_FOR_ESTIMATE; j++) {
		mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
		start = jiffies;
		for (i = 0; i < TEST_RANGE_COUNT; i++) {
			if (ranges[i].len == 0)
				continue;

			ex.fe_start = ranges[i].start;
			ex.fe_len = ranges[i].len;
			ext4_lock_group(sb, TEST_GOAL_GROUP);
			mb_mark_used_test(&e4b, &ex);
			ext4_unlock_group(sb, TEST_GOAL_GROUP);
		}
		end = jiffies;
		all += (end - start);

		for (i = 0; i < TEST_RANGE_COUNT; i++) {
			if (ranges[i].len == 0)
				continue;

			ext4_lock_group(sb, TEST_GOAL_GROUP);
			mb_free_blocks_test(NULL, &e4b, ranges[i].start,
				       ranges[i].len);
			ext4_unlock_group(sb, TEST_GOAL_GROUP);
		}
	}

	kunit_info(test, "costed jiffies %lu\n", all);
	ext4_mb_unload_buddy_test(&e4b);
}

static void test_mb_bit_ops(struct kunit *test)
{
	u8 *buf;
	void *unaligned;
	int bit, i;

	buf = kunit_kzalloc(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/*
	 * Use an unaligned pointer (offset by 3 bytes) to exercise
	 * mb_correct_addr_and_bit() on 64-bit/32-bit architectures.
	 */
	unaligned = buf + 3;

	mb_set_bit_test(0, unaligned);
	mb_set_bit_test(7, unaligned);
	mb_set_bit_test(64, unaligned);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(0, unaligned), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(1, unaligned), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(7, unaligned), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(64, unaligned), 0);

	mb_clear_bit_test(7, unaligned);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(7, unaligned), 0);

	bit = mb_find_next_bit_test(unaligned, 128, 1);
	KUNIT_EXPECT_EQ(test, bit, 64);

	/* Clamping when no set bit exists before max */
	bit = mb_find_next_bit_test(unaligned, 60, 1);
	KUNIT_EXPECT_EQ(test, bit, 60);

	/* Zero-length mb_set_bits / mb_clear_bits should be a no-op */
	memset(buf, 0, 128);
	mb_set_bits(buf, 10, 0);
	KUNIT_EXPECT_EQ(test, mb_find_next_bit_test(buf, 256, 0), 256);

	/* Multi-word range spanning head bits, 32-bit words, and tail */
	mb_set_bits(buf, 5, 75);
	KUNIT_EXPECT_EQ(test, mb_find_next_bit_test(buf, 256, 0), 5);
	KUNIT_EXPECT_EQ(test, mb_find_next_zero_bit_test(buf, 256, 5), 80);
	/* Clamping when no zero bit exists before max */
	KUNIT_EXPECT_EQ(test, mb_find_next_zero_bit_test(buf, 50, 5), 50);

	/* Clear a sub-range across 32-bit boundaries */
	mb_clear_bits_test(buf, 13, 50);
	for (i = 0; i < 100; i++) {
		int expected = (i >= 5 && i < 13) || (i >= 63 && i < 80);

		KUNIT_EXPECT_EQ(test, !!mb_test_bit_test(i, buf), expected);
	}
}

static void test_mb_test_and_clear_bits(struct kunit *test)
{
	u8 *buf;
	void *bm;
	int ret, i;

	buf = kunit_kzalloc(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	bm = buf;

	/* Case 1: All bits in range [5, 85) are set -> returns -1 and clears all */
	memset(buf, 0xff, 128);
	ret = mb_test_and_clear_bits_test(bm, 5, 80);
	KUNIT_EXPECT_EQ(test, ret, -1);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(4, bm), 0);
	for (i = 5; i < 85; i++)
		KUNIT_EXPECT_EQ(test, mb_test_bit_test(i, bm), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(85, bm), 0);

	/* Case 2: Zero bit in the unaligned head (bit 7) */
	memset(buf, 0xff, 128);
	mb_clear_bit_test(7, bm);
	ret = mb_test_and_clear_bits_test(bm, 5, 80);
	KUNIT_EXPECT_EQ(test, ret, 7);
	for (i = 5; i < 85; i++)
		KUNIT_EXPECT_EQ(test, mb_test_bit_test(i, bm), 0);

	/* Case 3: Zero bit inside the 32-bit word fast-path region (bit 45) */
	memset(buf, 0xff, 128);
	mb_clear_bit_test(45, bm);
	ret = mb_test_and_clear_bits_test(bm, 5, 80);
	KUNIT_EXPECT_EQ(test, ret, 45);
	for (i = 5; i < 85; i++)
		KUNIT_EXPECT_EQ(test, mb_test_bit_test(i, bm), 0);
}

static void test_mb_find_buddy_and_order(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_free_extent ex;
	void *bb;
	int max = 0, ret;
	int ord0, ord1, ord2, ord3;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Order 0 returns bitmap and bit count */
	bb = mb_find_buddy_test(&e4b, 0, &max);
	KUNIT_EXPECT_PTR_EQ(test, bb, e4b.bd_bitmap);
	KUNIT_EXPECT_EQ(test, max, (int)(sb->s_blocksize << 3));

	/* Valid higher order returns buddy offset */
	bb = mb_find_buddy_test(&e4b, 1, &max);
	KUNIT_EXPECT_PTR_EQ(test, bb, e4b.bd_buddy + sbi->s_mb_offsets[1]);
	KUNIT_EXPECT_EQ(test, max, sbi->s_mb_maxs[1]);

	/* Out-of-bounds order (> bd_blkbits + 1) returns NULL and max = 0 */
	bb = mb_find_buddy_test(&e4b, MB_NUM_ORDERS(sb), &max);
	KUNIT_EXPECT_NULL(test, bb);
	KUNIT_EXPECT_EQ(test, max, 0);

	/*
	 * Mark cluster 0 used in group 1: this splits the group so that:
	 * - cluster 1 is free at order 0
	 * - clusters [2, 4) are free at order 1
	 * - clusters [4, 8) are free at order 2
	 * - clusters [8, 16) are free at order 3
	 */
	ex.fe_group = TEST_GOAL_GROUP;
	ex.fe_start = 0;
	ex.fe_len = 1;
	ext4_lock_group(sb, TEST_GOAL_GROUP);
	mb_mark_used_test(&e4b, &ex);
	ord0 = mb_find_order_for_block_test(&e4b, 1);
	ord1 = mb_find_order_for_block_test(&e4b, 2);
	ord2 = mb_find_order_for_block_test(&e4b, 4);
	ord3 = mb_find_order_for_block_test(&e4b, 8);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_EQ(test, ord0, 0);
	KUNIT_EXPECT_EQ(test, ord1, 1);
	KUNIT_EXPECT_EQ(test, ord2, 2);
	KUNIT_EXPECT_EQ(test, ord3, 3);

	ext4_mb_unload_buddy_test(&e4b);
}

static void test_mb_find_extent(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_buddy e4b;
	struct ext4_free_extent ex, found_used, found_small, found_coalesced, found_unaligned;
	int ret, len_used, len_small, len_coalesced, len_unaligned;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * Leave only [10, 26) (16 clusters) free:
	 * [10, 12) order 1 (2) + [12, 16) order 2 (4) +
	 * [16, 24) order 3 (8) + [24, 26) order 1 (2).
	 */
	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ex.fe_group = TEST_GOAL_GROUP;
	ex.fe_start = 0;
	ex.fe_len = 10;
	mb_mark_used_test(&e4b, &ex);

	ex.fe_start = 26;
	ex.fe_len = EXT4_CLUSTERS_PER_GROUP(sb) - 26;
	mb_mark_used_test(&e4b, &ex);

	len_used = mb_find_extent_test(&e4b, 5, 8, &found_used);
	len_small = mb_find_extent_test(&e4b, 10, 1, &found_small);
	len_coalesced = mb_find_extent_test(&e4b, 10, 20, &found_coalesced);
	len_unaligned = mb_find_extent_test(&e4b, 11, 20, &found_unaligned);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	/* Used block returns 0 */
	KUNIT_EXPECT_EQ(test, len_used, 0);
	KUNIT_EXPECT_EQ(test, found_used.fe_len, 0);

	/* Needed = 1 at block 10 returns the order-1 buddy [10, 12) */
	KUNIT_EXPECT_EQ(test, len_small, 2);
	KUNIT_EXPECT_EQ(test, found_small.fe_start, 10);
	KUNIT_EXPECT_EQ(test, found_small.fe_len, 2);

	/* Needed = 20 at block 10 coalesces across orders 1, 2, 3, 1 -> 16 clusters */
	KUNIT_EXPECT_EQ(test, len_coalesced, 16);
	KUNIT_EXPECT_EQ(test, found_coalesced.fe_start, 10);
	KUNIT_EXPECT_EQ(test, found_coalesced.fe_len, 16);

	/* Unaligned start at block 11 coalesces [11, 26) -> 15 clusters */
	KUNIT_EXPECT_EQ(test, len_unaligned, 15);
	KUNIT_EXPECT_EQ(test, found_unaligned.fe_start, 11);
	KUNIT_EXPECT_EQ(test, found_unaligned.fe_len, 15);

	ext4_mb_unload_buddy_test(&e4b);
}

static void test_mb_generate_buddy_corrupt(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_group_info *grp;
	void *bitmap, *buddy;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	bool is_corrupt;
	ext4_grpblk_t corrected_free;
	int i;

	bitmap = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bitmap);
	buddy = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buddy);

	grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	KUNIT_ASSERT_NOT_NULL(test, grp);

	/* Mark 10 clusters used in bitmap, so actual free is max - 10 */
	mb_set_bits(bitmap, max, sb->s_blocksize * 8 - max);
	mb_set_bits(bitmap, 0, 10);

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	/* Intentionally set wrong bb_free = max (mismatch with max - 10) */
	grp->bb_free = max;
	memset(buddy, 0xff, sb->s_blocksize);
	ext4_mb_generate_buddy_test(sb, buddy, bitmap, TEST_GOAL_GROUP, grp);
	is_corrupt = EXT4_MB_GRP_BBITMAP_CORRUPT(grp);
	corrected_free = grp->bb_free;
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_TRUE(test, is_corrupt);
	KUNIT_EXPECT_EQ(test, corrected_free, max - 10);
}

static void test_mb_free_blocks_double_free_and_fc_replay(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_free_extent ex;
	bool corrupt_normal, corrupt_fc;
	ext4_grpblk_t free_after_fc;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	/* Part 1: Normal mode double-free marks group bitmap corrupt */
	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	/* Clusters [10, 14) are already free; freeing them again is a double free */
	mb_free_blocks_test(NULL, &e4b, 10, 4);
	corrupt_normal = EXT4_MB_GRP_BBITMAP_CORRUPT(e4b.bd_info);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_unload_buddy_test(&e4b);

	KUNIT_EXPECT_TRUE(test, corrupt_normal);

	/*
	 * Part 2: In EXT4_FC_REPLAY mode, freeing an already-freed range
	 * triggers mb_regenerate_buddy() instead of marking the group corrupt.
	 */
	ret = ext4_mb_load_buddy_test(sb, 2, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ext4_lock_group(sb, 2);
	ex.fe_group = 2;
	ex.fe_start = 10;
	ex.fe_len = 10;
	mb_mark_used_test(&e4b, &ex);
	/* Free sub-range [12, 16) first so [12, 16) is already free */
	mb_free_blocks_test(NULL, &e4b, 12, 4);

	/* Re-freeing [12, 16) in EXT4_FC_REPLAY mode regenerates buddy cleanly */
	sbi->s_mount_state |= EXT4_FC_REPLAY;
	mb_free_blocks_test(NULL, &e4b, 12, 4);
	sbi->s_mount_state &= ~EXT4_FC_REPLAY;

	corrupt_fc = EXT4_MB_GRP_BBITMAP_CORRUPT(e4b.bd_info);
	free_after_fc = e4b.bd_info->bb_free;
	ext4_unlock_group(sb, 2);
	ext4_mb_unload_buddy_test(&e4b);

	KUNIT_EXPECT_FALSE(test, corrupt_fc);
	KUNIT_EXPECT_EQ(test, free_after_fc, EXT4_CLUSTERS_PER_GROUP(sb) - 6);
}

static void test_mb_xarray_orders_and_frag_size(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_info *grp;
	int ord_1, ord_4, ord_7, ord_8;
	int old_lfo, new_lfo, new_afso;
	void *xa_lfo_entry, *xa_afso_entry, *xa_old_entry;
	int i;

	set_opt2(sb, MB_OPTIMIZE_SCAN);

	/* mb_avg_fragment_size_order computes clamp(fls(len) - 2, 0, MB_NUM_ORDERS - 1) */
	ord_1 = mb_avg_fragment_size_order_test(sb, 1);
	ord_4 = mb_avg_fragment_size_order_test(sb, 4);
	ord_7 = mb_avg_fragment_size_order_test(sb, 7);
	ord_8 = mb_avg_fragment_size_order_test(sb, 8);
	KUNIT_EXPECT_EQ(test, ord_1, 0);
	KUNIT_EXPECT_EQ(test, ord_4, 1);
	KUNIT_EXPECT_EQ(test, ord_7, 1);
	KUNIT_EXPECT_EQ(test, ord_8, 2);

	grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	KUNIT_ASSERT_NOT_NULL(test, grp);

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	old_lfo = grp->bb_largest_free_order;
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	grp->bb_counters[3] = 2;
	grp->bb_free = 16;
	grp->bb_fragments = 2;

	mb_set_largest_free_order_test(sb, grp);
	mb_update_avg_fragment_size_test(sb, grp);

	new_lfo = grp->bb_largest_free_order;
	new_afso = grp->bb_avg_fragment_size_order;
	xa_lfo_entry = xa_load(&sbi->s_mb_largest_free_orders[3], TEST_GOAL_GROUP);
	xa_afso_entry = xa_load(&sbi->s_mb_avg_fragment_size[2], TEST_GOAL_GROUP);

	/* Now mark group completely full and verify XArray entries are erased */
	grp->bb_counters[3] = 0;
	grp->bb_free = 0;
	grp->bb_fragments = 0;
	mb_set_largest_free_order_test(sb, grp);
	mb_update_avg_fragment_size_test(sb, grp);
	xa_old_entry = xa_load(&sbi->s_mb_largest_free_orders[3], TEST_GOAL_GROUP);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_EQ(test, old_lfo, -1);
	KUNIT_EXPECT_EQ(test, new_lfo, 3);
	KUNIT_EXPECT_EQ(test, new_afso, 2);
	KUNIT_EXPECT_PTR_EQ(test, xa_lfo_entry, grp);
	KUNIT_EXPECT_PTR_EQ(test, xa_afso_entry, grp);
	KUNIT_EXPECT_NULL(test, xa_old_entry);
}

static void test_mb_good_group(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_allocation_context ac = {};
	struct ext4_group_info *grp1, *grp2;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	/* Initialize group 1 buddy cache so bb_fragments and orders are populated */
	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);

	grp1 = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	KUNIT_ASSERT_NOT_NULL(test, grp1);

	ac.ac_sb = sb;
	ac.ac_2order = 3;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_flags = EXT4_MB_HINT_DATA;

	/* Group 1 has all clusters free in 1 fragment -> eligible at all CRs */
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_POWER2_ALIGNED));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_GOAL_LEN_FAST));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_BEST_AVAIL_LEN));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_GOAL_LEN_SLOW));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_ANY_FREE));

	/*
	 * Simulate high fragmentation: free = 16, fragments = 8 (avg = 2 < 8),
	 * largest_free_order = 1 (< ac_2order = 3).
	 * CR_POWER2_ALIGNED, CR_GOAL_LEN_FAST, CR_BEST_AVAIL_LEN must reject,
	 * while CR_GOAL_LEN_SLOW (free 16 >= 8) and CR_ANY_FREE accept.
	 */
	ext4_lock_group(sb, TEST_GOAL_GROUP);
	grp1->bb_free = 16;
	grp1->bb_fragments = 8;
	grp1->bb_largest_free_order = 1;
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_FALSE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							 CR_POWER2_ALIGNED));
	KUNIT_EXPECT_FALSE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							 CR_GOAL_LEN_FAST));
	KUNIT_EXPECT_FALSE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							 CR_BEST_AVAIL_LEN));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_GOAL_LEN_SLOW));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_ANY_FREE));

	/* When free = 4 < fe_len = 8, CR_GOAL_LEN_SLOW rejects, CR_ANY_FREE accepts */
	ext4_lock_group(sb, TEST_GOAL_GROUP);
	grp1->bb_free = 4;
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_FALSE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							 CR_GOAL_LEN_SLOW));
	KUNIT_EXPECT_TRUE(test, ext4_mb_good_group_test(&ac, TEST_GOAL_GROUP,
							CR_ANY_FREE));

	/* Flex_bg first group avoidance at CR_POWER2_ALIGNED for data */
	ret = ext4_mb_load_buddy_test(sb, 0, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);
	sbi->s_log_groups_per_flex = 2; /* flex_size = 4 >= EXT4_FLEX_SIZE_DIR_ALLOC_SCHEME */
	KUNIT_EXPECT_FALSE(test, ext4_mb_good_group_test(&ac, 0, CR_POWER2_ALIGNED));
	sbi->s_log_groups_per_flex = 0;

	/*
	 * Test ext4_mb_good_group_nolock on an uninitialized group on a
	 * separate buddy-cache folio:
	 * At inexpensive CR_POWER2_ALIGNED, it skips initializing the group
	 * and returns 0. At expensive CR_GOAL_LEN_SLOW (with STRICT_CHECK),
	 * it initializes the group and returns 1.
	 */
	{
		ext4_group_t uninit_grp = max_t(ext4_group_t, 2,
			DIV_ROUND_UP(PAGE_SIZE / sb->s_blocksize, 2));

		if (uninit_grp < ext4_get_groups_count(sb)) {
			grp2 = ext4_get_group_info(sb, uninit_grp);
			KUNIT_ASSERT_NOT_NULL(test, grp2);
			KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp2));

			ac.ac_criteria = CR_POWER2_ALIGNED;
			ret = ext4_mb_good_group_nolock_test(&ac, uninit_grp,
							     CR_POWER2_ALIGNED);
			KUNIT_EXPECT_EQ(test, ret, 0);
			KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp2));

			ac.ac_flags |= EXT4_MB_STRICT_CHECK;
			ac.ac_criteria = CR_GOAL_LEN_SLOW;
			ret = ext4_mb_good_group_nolock_test(&ac, uninit_grp,
							     CR_GOAL_LEN_SLOW);
			KUNIT_EXPECT_EQ(test, ret, 1);
			KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_NEED_INIT(grp2));
		}
	}
}

static void test_mb_find_by_goal(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_allocation_context ac = {};
	struct inode *inode;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/* Mark [24, 32) used in group 1 so [20, 24) has only 4 free clusters */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 24, 8);
	ext4_get_group_info(sb, TEST_GOAL_GROUP)->bb_free -= 8;

	/* 1. Exact goal hit at [10, 14) */
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL;
	ac.ac_g_ex.fe_group = TEST_GOAL_GROUP;
	ac.ac_g_ex.fe_start = 10;
	ac.ac_g_ex.fe_len = 4;
	ac.ac_o_ex = ac.ac_g_ex;

	ret = ext4_mb_find_by_goal_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_group, TEST_GOAL_GROUP);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 10);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 4);

	/* 2. Partial goal at [20, 28) without HINT_MERGE fails to match */
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL;
	ac.ac_g_ex.fe_group = TEST_GOAL_GROUP;
	ac.ac_g_ex.fe_start = 20;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_o_ex = ac.ac_g_ex;

	ret = ext4_mb_find_by_goal_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_CONTINUE);

	/* 3. Partial goal at [20, 28) WITH EXT4_MB_HINT_MERGE allocates [20, 24) */
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL | EXT4_MB_HINT_MERGE;
	ac.ac_g_ex.fe_group = TEST_GOAL_GROUP;
	ac.ac_g_ex.fe_start = 20;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_o_ex = ac.ac_g_ex;

	ret = ext4_mb_find_by_goal_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 20);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 4);

	/*
	 * 4. Stripe-aligned check: s_stripe = 8 clusters; unaligned start 36
	 * rejected, aligned 40 accepted.
	 */
	sbi->s_stripe = EXT4_C2B(sbi, 8);
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL;
	ac.ac_g_ex.fe_group = TEST_GOAL_GROUP;
	ac.ac_g_ex.fe_start = 36;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_o_ex = ac.ac_g_ex;

	ret = ext4_mb_find_by_goal_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_CONTINUE);

	ac.ac_g_ex.fe_start = 40;
	ac.ac_o_ex = ac.ac_g_ex;
	ret = ext4_mb_find_by_goal_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 40);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 8);
	sbi->s_stripe = 0;
}

static void test_mb_simple_and_aligned_scan(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_allocation_context ac = {};
	struct inode *inode;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/*
	 * Mark cluster 0 used in group 1 so:
	 * - order 3 buddy [8, 16) is the first order-3 buddy
	 * - first 16-cluster stripe [0, 16) is not completely free, so
	 *   stripe-aligned scan must skip to cluster 16.
	 */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 0, 1);
	ext4_get_group_info(sb, TEST_GOAL_GROUP)->bb_free--;

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* 1. ext4_mb_simple_scan_group with ac_2order = 3 (8 clusters) */
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_criteria = CR_POWER2_ALIGNED;
	ac.ac_2order = 3;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_o_ex.fe_len = 8;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_simple_scan_group_test(&ac, &e4b);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_release_context_test(&ac);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_group, TEST_GOAL_GROUP);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 8);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 8);

	/* 2. ext4_mb_scan_aligned with stripe = 16 clusters */
	sbi->s_stripe = EXT4_C2B(sbi, 16);
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_criteria = CR_GOAL_LEN_FAST;
	ac.ac_g_ex.fe_len = 16;
	ac.ac_o_ex.fe_len = 16;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_scan_aligned_test(&ac, &e4b);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_release_context_test(&ac);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_group, TEST_GOAL_GROUP);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 16);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 16);

	sbi->s_stripe = 0;
	ext4_mb_unload_buddy_test(&e4b);
}

static void test_mb_complex_scan_and_measure(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_allocation_context ac = {};
	struct inode *inode;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/*
	 * Create 3 power-of-2 aligned free extents in TEST_GOAL_GROUP:
	 * - [0, 4)   len 4  (order 2 buddy)
	 * - [16, 32) len 16 (order 4 buddy)
	 * - [40, 48) len 8  (order 3 buddy)
	 * Mark everything else used (total free = 28 clusters).
	 */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 4, 12);
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 32, 8);
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 48, max - 48);
	ext4_get_group_info(sb, TEST_GOAL_GROUP)->bb_free = 28;

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * Case 1: Goal len = 5 at CR_GOAL_LEN_SLOW.
	 * Scans [0, 4) (len 4 < 5), [16, 32) (order 4 buddy -> len 16 > 5),
	 * and [40, 48) (order 3 buddy -> len 8 > 5, smaller than 16 so preferred!),
	 * then allocates 5 clusters from [40, 48) at finish_group.
	 */
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_criteria = CR_GOAL_LEN_SLOW;
	ac.ac_g_ex.fe_len = 5;
	ac.ac_o_ex.fe_len = 5;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_complex_scan_group_test(&ac, &e4b);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_release_context_test(&ac);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 40);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 5);

	/*
	 * Case 2: Goal len = 32 (larger than any free extent) with s_mb_max_to_scan = 1.
	 * Scanning [0, 4) and [16, 32) increments ac_found to 2 > s_mb_max_to_scan,
	 * triggering AC_STATUS_BREAK with best smaller extent [16, 32) (len 16)
	 * recorded in ac_b_ex. Calling ext4_mb_try_best_found() then claims [16, 32).
	 */
	sbi->s_mb_max_to_scan = 1;
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_criteria = CR_ANY_FREE;
	ac.ac_g_ex.fe_len = 32;
	ac.ac_o_ex.fe_len = 32;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_complex_scan_group_test(&ac, &e4b);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_BREAK);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 16);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 16);

	ext4_mb_try_best_found_test(&ac, &e4b);
	ext4_mb_release_context_test(&ac);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 16);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 16);

	/*
	 * Case 3: EXT4_MB_HINT_FIRST immediately takes the first free extent [0, 4).
	 */
	sbi->s_mb_max_to_scan = 200;
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_criteria = CR_ANY_FREE;
	ac.ac_flags = EXT4_MB_HINT_FIRST;
	ac.ac_g_ex.fe_len = 4;
	ac.ac_o_ex.fe_len = 4;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_complex_scan_group_test(&ac, &e4b);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);
	ext4_mb_release_context_test(&ac);

	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_start, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 4);

	ext4_mb_unload_buddy_test(&e4b);
}

static void test_mb_regular_allocator(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_allocation_context ac = {};
	struct inode *inode;
	int ret, hash;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/*
	 * 1. EXT4_MB_HINT_GOAL_ONLY when goal block (group 0, cluster 0) is already
	 * marked used: must stop immediately without scanning other blocks.
	 */
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_GOAL_ONLY;
	ac.ac_g_ex.fe_group = 0;
	ac.ac_g_ex.fe_start = 0;
	ac.ac_g_ex.fe_len = 1;
	ac.ac_o_ex = ac.ac_g_ex;

	ret = ext4_mb_regular_allocator_test(&ac);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_CONTINUE);

	/*
	 * 2. Stream allocation (EXT4_MB_STREAM_ALLOC) with group 0 full:
	 * allocates in group 1 and updates s_mb_last_groups[hash] to 1.
	 */
	mbt_ctx_mark_used(sb, 0, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 0)->desc, 0);
	ext4_get_group_info(sb, 0)->bb_free = 0;

	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_STREAM_ALLOC | EXT4_MB_HINT_DATA;
	ac.ac_g_ex.fe_len = 8;
	ac.ac_orig_goal_len = 8;
	ac.ac_o_ex.fe_len = 8;

	ret = ext4_mb_regular_allocator_test(&ac);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_group, 1);

	hash = (unsigned int)inode->i_ino % sbi->s_mb_nr_global_goals;
	KUNIT_EXPECT_EQ(test, READ_ONCE(sbi->s_mb_last_groups[hash]), 1);

	/*
	 * 3. MB_OPTIMIZE_SCAN with CR_BEST_AVAIL_LEN goal length trimming:
	 * Make group 2 the only group with free blocks, fragmented into two
	 * 8-cluster chunks [0, 8) and [16, 24) so largest_free_order = 3
	 * and avg_fragment_size = 8.
	 * Note: group 2 may have already been prefetched during step 2, so
	 * mark its blocks used via mb_mark_used_test on the loaded buddy.
	 */
	set_opt2(sb, MB_OPTIMIZE_SCAN);
	mbt_ctx_mark_used(sb, 1, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 1)->desc, 0);
	ext4_get_group_info(sb, 1)->bb_free = 0;
	ext4_get_group_info(sb, 1)->bb_fragments = 0;
	mb_set_largest_free_order_test(sb, ext4_get_group_info(sb, 1));
	mb_update_avg_fragment_size_test(sb, ext4_get_group_info(sb, 1));

	mbt_ctx_mark_used(sb, 3, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 3)->desc, 0);
	ext4_get_group_info(sb, 3)->bb_free = 0;
	ext4_get_group_info(sb, 3)->bb_fragments = 0;
	mb_set_largest_free_order_test(sb, ext4_get_group_info(sb, 3));
	mb_update_avg_fragment_size_test(sb, ext4_get_group_info(sb, 3));

	{
		struct ext4_buddy e4b;
		struct ext4_free_extent ex;

		ret = ext4_mb_load_buddy_test(sb, 2, &e4b);
		KUNIT_ASSERT_EQ(test, ret, 0);
		ext4_lock_group(sb, 2);
		ex.fe_group = 2;
		ex.fe_start = 8;
		ex.fe_len = 8;
		mb_mark_used_test(&e4b, &ex);
		ex.fe_start = 24;
		ex.fe_len = EXT4_CLUSTERS_PER_GROUP(sb) - 24;
		mb_mark_used_test(&e4b, &ex);
		ext4_unlock_group(sb, 2);
		ext4_mb_unload_buddy_test(&e4b);
	}

	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_DATA;
	ac.ac_g_ex.fe_group = 0;
	ac.ac_g_ex.fe_len = 16;
	ac.ac_orig_goal_len = 16;
	ac.ac_o_ex.fe_len = 4;

	ret = ext4_mb_pa_alloc_test(&ac);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = ext4_mb_regular_allocator_test(&ac);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_status, AC_STATUS_FOUND);
	KUNIT_EXPECT_EQ(test, ac.ac_criteria, CR_BEST_AVAIL_LEN);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_group, 2);
	KUNIT_EXPECT_EQ(test, ac.ac_f_ex.fe_len, 8);
	KUNIT_EXPECT_EQ(test, ac.ac_b_ex.fe_len, 4);
	ext4_discard_preallocations(inode);
}

static void test_mb_prefetch(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_group_info *grp0, *grp1, *grp2;
	ext4_group_t next_grp, uninit_grp;
	int ios = 0;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	uninit_grp = max_t(ext4_group_t, 2,
			   DIV_ROUND_UP(PAGE_SIZE / sb->s_blocksize, 2));

	grp0 = ext4_get_group_info(sb, 0);
	grp1 = ext4_get_group_info(sb, 1);
	grp2 = ext4_get_group_info(sb, 2);
	KUNIT_ASSERT_NOT_NULL(test, grp0);
	KUNIT_ASSERT_NOT_NULL(test, grp1);
	KUNIT_ASSERT_NOT_NULL(test, grp2);

	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp0));
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp1));
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp2));

	next_grp = ext4_mb_prefetch(sb, 0, 2, &ios);
	KUNIT_EXPECT_EQ(test, next_grp, 2);
	/* First prefetch of uninitialized buffers increments ios count */
	KUNIT_EXPECT_EQ(test, ios, 2);

	/* Repeated prefetch on the same groups does not re-read bitmaps */
	ios = 0;
	ext4_mb_prefetch(sb, 0, 2, &ios);
	KUNIT_EXPECT_EQ(test, ios, 0);

	/* ext4_mb_prefetch_fini initializes the prefetched groups [0, 2) */
	ext4_mb_prefetch_fini(sb, next_grp, 2);
	KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_NEED_INIT(grp0));
	KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_NEED_INIT(grp1));
	if (uninit_grp < ext4_get_groups_count(sb)) {
		grp2 = ext4_get_group_info(sb, uninit_grp);
		KUNIT_ASSERT_NOT_NULL(test, grp2);
		KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_NEED_INIT(grp2));
	}
}

static void test_mb_initialize_context(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_allocation_context ac = {};
	struct ext4_allocation_request ar = {};
	struct inode *inode;

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/* 1. Small open file (<= s_mb_stream_request) uses locality group alloc */
	i_size_write(inode, sb->s_blocksize);
	ar.inode = inode;
	ar.logical = 0;
	ar.len = 1;
	ar.goal = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	ar.flags = EXT4_MB_HINT_DATA;

	ext4_mb_initialize_context_test(&ac, &ar);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_NE(test, ac.ac_flags & EXT4_MB_HINT_GROUP_ALLOC, 0);
	KUNIT_EXPECT_NOT_NULL(test, ac.ac_lg);
	KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_group, TEST_GOAL_GROUP);

	/* 2. Large open file (> s_mb_stream_request) uses stream alloc */
	memset(&ac, 0, sizeof(ac));
	i_size_write(inode, (loff_t)(sbi->s_mb_stream_request + 64) << sb->s_blocksize_bits);
	ext4_mb_initialize_context_test(&ac, &ar);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_NE(test, ac.ac_flags & EXT4_MB_STREAM_ALLOC, 0);
	KUNIT_EXPECT_NULL(test, ac.ac_lg);

	/*
	 * 3. Closed file (i_writecount == 0) with size == isize and
	 * size > s_mb_stream_request sets EXT4_MB_HINT_NOPREALLOC.
	 */
	memset(&ac, 0, sizeof(ac));
	atomic_set(&inode->i_writecount, 0);
	ar.logical = EXT4_LBLK_CMASK(sbi, sbi->s_mb_stream_request + EXT4_C2B(sbi, 2));
	ar.len = 1;
	i_size_write(inode, (loff_t)(ar.logical + EXT4_C2B(sbi, 1)) << sb->s_blocksize_bits);
	/* Also test out-of-range ar.goal fallback to group 0 */
	ar.goal = ext4_blocks_count(sbi->s_es) + 100;
	ext4_mb_initialize_context_test(&ac, &ar);
	ext4_mb_release_context_test(&ac);
	KUNIT_EXPECT_NE(test, ac.ac_flags & EXT4_MB_HINT_NOPREALLOC, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_o_ex.fe_group, 0);
	KUNIT_EXPECT_EQ(test, ac.ac_o_ex.fe_start, 0);
}

static void test_mb_normalize_request(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_allocation_context ac = {};
	struct ext4_allocation_request ar = {};
	struct inode *inode;
	struct ext4_inode_info *ei;
	struct ext4_prealloc_space left_pa = {}, right_pa = {};
	int bsbits = sb->s_blocksize_bits;

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);
	ei = EXT4_I(inode);

	/* 1. Metadata request (no EXT4_MB_HINT_DATA) is not normalized */
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_o_ex.fe_logical = 0;
	ac.ac_o_ex.fe_len = 1;
	ac.ac_g_ex = ac.ac_o_ex;
	ext4_mb_normalize_request_test(&ac, &ar);
	KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_len, 1);

	/* 2. Locality group request normalizes fe_len to s_mb_group_prealloc */
	memset(&ac, 0, sizeof(ac));
	ac.ac_sb = sb;
	ac.ac_inode = inode;
	ac.ac_flags = EXT4_MB_HINT_DATA | EXT4_MB_HINT_GROUP_ALLOC;
	ac.ac_lg = raw_cpu_ptr(sbi->s_locality_groups);
	ac.ac_o_ex.fe_len = 1;
	ac.ac_g_ex = ac.ac_o_ex;
	ext4_mb_normalize_request_test(&ac, &ar);
	KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_len, sbi->s_mb_group_prealloc);

	/* 3. Small file (<= 16KB) normalizes up to max(SZ_16K, cluster_size) */
	if ((SZ_16K >> bsbits) >= sbi->s_cluster_ratio) {
		memset(&ac, 0, sizeof(ac));
		i_size_write(inode, 4096);
		ac.ac_sb = sb;
		ac.ac_inode = inode;
		ac.ac_flags = EXT4_MB_HINT_DATA;
		ac.ac_o_ex.fe_logical = 0;
		ac.ac_o_ex.fe_len = 1;
		ac.ac_g_ex = ac.ac_o_ex;
		ext4_mb_normalize_request_test(&ac, &ar);
		KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_len,
				EXT4_NUM_B2C(sbi, SZ_16K >> bsbits));
	}

	/* 4. 200KB file normalizes up to next power of two (256KB) */
	if ((SZ_256K >> bsbits) >= sbi->s_cluster_ratio) {
		memset(&ac, 0, sizeof(ac));
		i_size_write(inode, 200 * 1024);
		ac.ac_sb = sb;
		ac.ac_inode = inode;
		ac.ac_flags = EXT4_MB_HINT_DATA;
		ac.ac_o_ex.fe_logical = 0;
		ac.ac_o_ex.fe_len = 1;
		ac.ac_g_ex = ac.ac_o_ex;
		ext4_mb_normalize_request_test(&ac, &ar);
		KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_len,
				EXT4_NUM_B2C(sbi, SZ_256K >> bsbits));
	}

	/*
	 * 5. Trimming against left/right allocated neighbors (ar.pleft / ar.pright)
	 * and against existing inode preallocations (ext4_mb_pa_adjust_overlap).
	 */
	if (((loff_t)EXT4_C2B(sbi, 16) << bsbits) <= SZ_1M) {
		memset(&ac, 0, sizeof(ac));
		memset(&ar, 0, sizeof(ar));
		i_size_write(inode, (loff_t)EXT4_C2B(sbi, 16) << bsbits);
		ac.ac_sb = sb;
		ac.ac_inode = inode;
		ac.ac_flags = EXT4_MB_HINT_DATA;
		ac.ac_o_ex.fe_logical = EXT4_C2B(sbi, 4);
		ac.ac_o_ex.fe_len = 1;
		ac.ac_g_ex = ac.ac_o_ex;

		/* Left neighbor ends at cluster 2 (logical block EXT4_C2B(sbi, 2) - 1) */
		ar.lleft = EXT4_C2B(sbi, 2) - 1;
		ar.pleft = ext4_group_first_block_no(sb, TEST_GOAL_GROUP) + ar.lleft;
		/* Right neighbor starts at cluster 10 */
		ar.lright = EXT4_C2B(sbi, 10);
		ar.pright = ext4_group_first_block_no(sb, TEST_GOAL_GROUP) + ar.lright;

		/*
		 * Also insert a left PA covering clusters [2, 3) and a right PA covering
		 * clusters [8, 10) into ei->i_prealloc_node so ext4_mb_pa_adjust_overlap
		 * trims the range further to clusters [3, 8) (5 clusters).
		 */
		spin_lock_init(&left_pa.pa_lock);
		left_pa.pa_lstart = EXT4_C2B(sbi, 2);
		left_pa.pa_len = 1;
		left_pa.pa_free = 1;
		left_pa.pa_deleted = 0;

		spin_lock_init(&right_pa.pa_lock);
		right_pa.pa_lstart = EXT4_C2B(sbi, 8);
		right_pa.pa_len = 2;
		right_pa.pa_free = 2;
		right_pa.pa_deleted = 0;

		rb_link_node(&left_pa.pa_node.inode_node, NULL,
			     &ei->i_prealloc_node.rb_node);
		rb_insert_color(&left_pa.pa_node.inode_node, &ei->i_prealloc_node);
		rb_link_node(&right_pa.pa_node.inode_node,
			     &left_pa.pa_node.inode_node,
			     &left_pa.pa_node.inode_node.rb_right);
		rb_insert_color(&right_pa.pa_node.inode_node, &ei->i_prealloc_node);

		ext4_mb_normalize_request_test(&ac, &ar);
		ei->i_prealloc_node = RB_ROOT;

		KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_logical, EXT4_C2B(sbi, 3));
		KUNIT_EXPECT_EQ(test, ac.ac_g_ex.fe_len, 5);
	}
}

static void test_mb_inode_pa_lifecycle(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	struct ext4_inode_info *ei;
	struct ext4_allocation_request ar = {};
	struct ext4_group_info *grp;
	ext4_group_t pa_grp;
	ext4_grpblk_t pa_off, free_before;
	ext4_fsblk_t blk1, blk2;
	void *pa_bitmap;
	int err = 0;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);
	ei = EXT4_I(inode);
	/* Large file size (> s_mb_stream_request) so inode PA is used */
	i_size_write(inode, (loff_t)(sbi->s_mb_stream_request + 64) << sb->s_blocksize_bits);

	ar.inode = inode;
	ar.logical = 0;
	ar.len = 1;
	ar.goal = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	ar.flags = EXT4_MB_HINT_DATA;

	blk1 = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_NE(test, blk1, 0ULL);
	KUNIT_EXPECT_EQ(test, ar.len, 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ei->i_prealloc_active), 1);
	KUNIT_EXPECT_FALSE(test, RB_EMPTY_ROOT(&ei->i_prealloc_node));

	ext4_get_group_no_and_offset(sb, blk1, &pa_grp, &pa_off);
	grp = ext4_get_group_info(sb, pa_grp);
	free_before = ext4_free_group_clusters(sb, &MBT_GRP_CTX(sb, pa_grp)->desc) + 1;

	/* Verify ext4_mb_generate_from_pa marks the PA's clusters in a bitmap */
	pa_bitmap = kunit_kzalloc(test, sb->s_blocksize, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pa_bitmap);
	ext4_lock_group(sb, pa_grp);
	ext4_mb_generate_from_pa_test(sb, pa_bitmap, pa_grp);
	ext4_unlock_group(sb, pa_grp);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(pa_off, pa_bitmap), 0);

	/* Second sequential allocation is served directly from the inode PA */
	ar.logical = EXT4_C2B(sbi, 1);
	ar.len = 1;
	ar.goal = blk1 + EXT4_C2B(sbi, 1);
	blk2 = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, blk2, blk1 + EXT4_C2B(sbi, 1));

	/* Discard inode preallocations and verify cleanup */
	ext4_discard_preallocations(inode);
	KUNIT_EXPECT_EQ(test, atomic_read(&ei->i_prealloc_active), 0);
	KUNIT_EXPECT_TRUE(test, RB_EMPTY_ROOT(&ei->i_prealloc_node));
	/* Only the 2 allocated clusters should remain used in the group */
	KUNIT_EXPECT_EQ(test, grp->bb_free, free_before - 2);
}

struct mbt_saved_affinity {
	cpumask_var_t mask;
};

static void mbt_restore_cpu_affinity(void *data)
{
	struct mbt_saved_affinity *saved = data;

	set_cpus_allowed_ptr(current, saved->mask);
	free_cpumask_var(saved->mask);
	kfree(saved);
}

static void mbt_pin_current_cpu(struct kunit *test)
{
	struct mbt_saved_affinity *saved;
	int err;

	saved = kzalloc_obj(*saved);
	KUNIT_ASSERT_NOT_NULL(test, saved);
	if (!alloc_cpumask_var(&saved->mask, GFP_KERNEL)) {
		kfree(saved);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc cpumask");
	}
	cpumask_copy(saved->mask, current->cpus_ptr);

	err = set_cpus_allowed_ptr(current,
				   cpumask_of(raw_smp_processor_id()));
	if (err) {
		free_cpumask_var(saved->mask);
		kfree(saved);
		KUNIT_ASSERT_EQ(test, err, 0);
	}

	err = kunit_add_action_or_reset(test, mbt_restore_cpu_affinity, saved);
	KUNIT_ASSERT_EQ(test, err, 0);
}

static void test_mb_group_pa_lifecycle(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode1, *inode2;
	struct ext4_allocation_request ar = {};
	struct ext4_group_info *grp;
	ext4_fsblk_t blk1, blk2;
	int err = 0, discarded;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	/* Pin to one CPU so both allocations hit the same locality group PA */
	mbt_pin_current_cpu(test);

	inode1 = mbt_alloc_test_inode(test, sb, S_IFREG);
	inode2 = mbt_alloc_test_inode(test, sb, S_IFREG);
	inode2->i_ino = 13;

	/* Small files (<= s_mb_stream_request) use locality group PA (MB_GROUP_PA) */
	i_size_write(inode1, sb->s_blocksize);
	i_size_write(inode2, sb->s_blocksize);

	ar.inode = inode1;
	ar.logical = 0;
	ar.len = 1;
	ar.goal = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	ar.flags = EXT4_MB_HINT_DATA;

	blk1 = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, blk1, ext4_group_first_block_no(sb, TEST_GOAL_GROUP));

	/* Second small file allocation reuses the locality group PA */
	ar.inode = inode2;
	ar.logical = 0;
	ar.len = 1;
	ar.goal = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	blk2 = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, blk2, blk1 + EXT4_C2B(sbi, 1));

	/* Discard all group preallocations and verify free count is restored */
	discarded = ext4_mb_discard_preallocations_test(sb, 0);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, discarded, (int)sbi->s_mb_group_prealloc - 2);

	grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	KUNIT_EXPECT_EQ(test, grp->bb_free, EXT4_CLUSTERS_PER_GROUP(sb) - 2);
}

static void test_mb_lg_list_trim(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_locality_group *lg;
	struct inode *inode;
	struct ext4_buddy e4b;
	struct ext4_prealloc_space *pa;
	int i, ret, count = 0;
	int order = fls(15) - 1; /* pa_free = 15 -> order 3 */

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);
	sbi->s_mb_group_prealloc = 16;

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * Create 9 locality-group PAs in bucket order 3 (each of length 16 with
	 * 1 cluster used -> pa_free = 15). When the 9th PA is added via
	 * ext4_mb_release_context -> ext4_mb_add_n_trim, lg_prealloc_count > 8
	 * triggers ext4_mb_discard_lg_preallocations() to trim the list to 5!
	 * Capture one locality group pointer up front so CPU migration across
	 * iterations cannot split PAs across different per-CPU locality groups.
	 */
	lg = raw_cpu_ptr(sbi->s_locality_groups);
	for (i = 0; i < 9; i++) {
		struct ext4_allocation_context ac = {};
		struct ext4_free_extent ex = {
			.fe_group = TEST_GOAL_GROUP,
			.fe_start = i * 32,
			.fe_len = 16,
		};

		ac.ac_sb = sb;
		ac.ac_inode = inode;
		ac.ac_flags = EXT4_MB_HINT_DATA | EXT4_MB_HINT_GROUP_ALLOC;
		ac.ac_lg = lg;
		mutex_lock(&ac.ac_lg->lg_mutex);

		ret = ext4_mb_pa_alloc_test(&ac);
		if (ret) {
			mutex_unlock(&ac.ac_lg->lg_mutex);
			ext4_mb_unload_buddy_test(&e4b);
			KUNIT_ASSERT_EQ(test, ret, 0);
		}

		ext4_lock_group(sb, TEST_GOAL_GROUP);
		mb_mark_used_test(&e4b, &ex);
		ext4_unlock_group(sb, TEST_GOAL_GROUP);

		ac.ac_status = AC_STATUS_FOUND;
		ac.ac_o_ex.fe_len = 1;
		ac.ac_b_ex = ex;
		ac.ac_f_ex = ex;
		ext4_mb_new_preallocation_test(&ac);
		/* Release context updates pa_free to 15 and runs ext4_mb_add_n_trim */
		ext4_mb_release_context_test(&ac);
	}

	ext4_mb_unload_buddy_test(&e4b);

	spin_lock(&lg->lg_prealloc_lock);
	list_for_each_entry(pa, &lg->lg_prealloc_list[order], pa_node.lg_list)
		count++;
	spin_unlock(&lg->lg_prealloc_lock);

	KUNIT_EXPECT_EQ(test, count, 5);
}

static void test_mb_new_blocks_and_enospc_retry(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode1, *inode2;
	struct ext4_allocation_request ar = {};
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	ext4_fsblk_t blk;
	int err = 0;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode1 = mbt_alloc_test_inode(test, sb, S_IFREG);
	inode2 = mbt_alloc_test_inode(test, sb, S_IFREG);
	inode2->i_ino = 14;
	i_size_write(inode1, sb->s_blocksize);

	/*
	 * Leave only 16 free clusters in group 1 and mark groups 0, 2, 3 full.
	 * Set s_mb_group_prealloc = 16 so allocating 1 cluster for inode1
	 * preallocates the remaining 15 clusters into a group PA (leaving
	 * 0 free clusters in buddy for all groups).
	 */
	mbt_ctx_mark_used(sb, 0, 0, max);
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 0)->desc, 0);
	ext4_get_group_info(sb, 0)->bb_free = 0;

	mbt_ctx_mark_used(sb, 1, 16, max - 16);
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 1)->desc, 16);
	ext4_get_group_info(sb, 1)->bb_free = 16;

	mbt_ctx_mark_used(sb, 2, 0, max);
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 2)->desc, 0);
	ext4_get_group_info(sb, 2)->bb_free = 0;

	mbt_ctx_mark_used(sb, 3, 0, max);
	ext4_free_group_clusters_set(sb, &MBT_GRP_CTX(sb, 3)->desc, 0);
	ext4_get_group_info(sb, 3)->bb_free = 0;

	percpu_counter_set(&sbi->s_freeclusters_counter, 16);
	sbi->s_mb_group_prealloc = 16;

	ar.inode = inode1;
	ar.logical = 0;
	ar.len = 1;
	ar.goal = ext4_group_first_block_no(sb, 1);
	ar.flags = EXT4_MB_HINT_DATA;

	blk = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ext4_get_group_info(sb, 1)->bb_free, 0);

	/*
	 * Now request 4 metadata clusters (no EXT4_MB_HINT_DATA so it cannot
	 * use the PA directly). First allocator pass finds 0 free clusters,
	 * then ext4_mb_discard_preallocations_should_retry() discards the PA
	 * (recovering 15 clusters) and retries, succeeding!
	 */
	memset(&ar, 0, sizeof(ar));
	ar.inode = inode2;
	ar.len = 4;
	ar.goal = ext4_group_first_block_no(sb, 1);
	ar.flags = 0;

	blk = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_NE(test, blk, 0ULL);
	KUNIT_EXPECT_EQ(test, ar.len, 4);

	/*
	 * The 4-cluster power-of-2 allocation claimed [4, 8), leaving two
	 * fragments [1, 4) (3 clusters) and [8, 16) (8 clusters).
	 * Requesting 11 clusters allocates the largest fragment (8 clusters),
	 * then requesting 3 clusters allocates [1, 4), and the next request
	 * returns -ENOSPC.
	 */
	ar.len = 11;
	blk = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ar.len, 8);

	ar.len = 3;
	blk = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ar.len, 3);

	ar.len = 1;
	blk = ext4_mb_new_blocks(NULL, &ar, &err);
	KUNIT_EXPECT_EQ(test, err, -ENOSPC);
	KUNIT_EXPECT_EQ(test, blk, 0ULL);
}

static int
ext4_handle_dirty_metadata_Updated_stub(const char *where, unsigned int line,
					handle_t *handle, struct inode *inode,
					struct buffer_head *bh)
{
	return 0;
}

static void test_mb_mark_context_and_mark_bb(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_desc *gdp1, *gdp2;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	ext4_grpblk_t changed = 0;
	ext4_fsblk_t boundary_blk;
	__u32 free_before;
	int ret;

	/* Test the real ext4_mb_mark_context implementation */
	kunit_deactivate_static_stub(test, ext4_mb_mark_context);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   ext4_handle_dirty_metadata_Updated_stub);

	gdp1 = ext4_get_group_desc(sb, 1, NULL);
	gdp2 = ext4_get_group_desc(sb, 2, NULL);
	KUNIT_ASSERT_NOT_NULL(test, gdp1);
	KUNIT_ASSERT_NOT_NULL(test, gdp2);
	free_before = ext4_free_group_clusters(sb, gdp1);

	/* 1. Mark [10, 20) used with EXT4_MB_BITMAP_MARKED_CHECK (0x1) */
	ret = ext4_mb_mark_context(NULL, sb, true, 1, 10, 10, 0x0001, &changed);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, changed, 10);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, gdp1), free_before - 10);

	/* 2. Mark overlapping [15, 25) used -> only 5 new clusters changed */
	ret = ext4_mb_mark_context(NULL, sb, true, 1, 15, 10, 0x0001, &changed);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, changed, 5);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, gdp1), free_before - 15);

	/* 3. Clear [10, 25) -> 15 clusters freed */
	ret = ext4_mb_mark_context(NULL, sb, false, 1, 10, 15, 0x0001, &changed);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, changed, 15);
	KUNIT_EXPECT_EQ(test, ext4_free_group_clusters(sb, gdp1), free_before);

	/*
	 * 4. Test ext4_mb_mark_bb across the boundary between group 1 and group 2:
	 * 2 clusters at the end of group 1 + 3 clusters at the start of group 2.
	 */
	kunit_activate_static_stub(test, ext4_mb_mark_context,
				   ext4_mb_mark_context_stub);
	boundary_blk = ext4_group_first_block_no(sb, 2) - EXT4_C2B(sbi, 2);
	ext4_mb_mark_bb(sb, boundary_blk, EXT4_C2B(sbi, 5), true);

	KUNIT_EXPECT_NE(test, mb_test_bit_test(max - 2, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(max - 1, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(0, mbt_ctx_bitmap(sb, 2)), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(2, mbt_ctx_bitmap(sb, 2)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(3, mbt_ctx_bitmap(sb, 2)), 0);

	ext4_mb_mark_bb(sb, boundary_blk, EXT4_C2B(sbi, 5), false);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(max - 2, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(0, mbt_ctx_bitmap(sb, 2)), 0);
}

static void test_ext4_free_blocks_flags_and_cross_group(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	struct ext4_buddy e4b;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	ext4_fsblk_t grp1_first = ext4_group_first_block_no(sb, 1);
	ext4_fsblk_t grp2_first = ext4_group_first_block_no(sb, 2);
	s64 free_before;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/* Mark clusters [10, 20) and [max - 2, max) in group 1, and [0, 3) in group 2 used */
	mbt_ctx_mark_used(sb, 1, 10, 10);
	mbt_ctx_mark_used(sb, 1, max - 2, 2);
	mbt_ctx_mark_used(sb, 2, 0, 3);
	ext4_get_group_info(sb, 1)->bb_free -= 12;
	ext4_get_group_info(sb, 2)->bb_free -= 3;

	ret = ext4_mb_load_buddy_test(sb, 1, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);
	ret = ext4_mb_load_buddy_test(sb, 2, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);

	/*
	 * 1. EXT4_FREE_BLOCKS_NOFREE_FIRST_CLUSTER | EXT4_FREE_BLOCKS_NOFREE_LAST_CLUSTER:
	 * Freeing from cluster 10 + 2 blocks through cluster 12 + 3 blocks
	 * skips partial clusters 10 and 12 and frees only full cluster 11!
	 */
	ext4_free_blocks(NULL, inode, NULL,
			 grp1_first + EXT4_C2B(sbi, 10) + 2,
			 EXT4_C2B(sbi, 2) - 2 + 3,
			 EXT4_FREE_BLOCKS_NOFREE_FIRST_CLUSTER |
			 EXT4_FREE_BLOCKS_NOFREE_LAST_CLUSTER |
			 EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);

	KUNIT_EXPECT_NE(test, mb_test_bit_test(10, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(11, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_NE(test, mb_test_bit_test(12, mbt_ctx_bitmap(sb, 1)), 0);

	/*
	 * 2. EXT4_FREE_BLOCKS_RERESERVE_CLUSTER: frees cluster 12 in bitmap & buddy
	 * without incrementing s_freeclusters_counter.
	 */
	free_before = percpu_counter_sum(&sbi->s_freeclusters_counter);
	ext4_free_blocks(NULL, inode, NULL,
			 grp1_first + EXT4_C2B(sbi, 12),
			 EXT4_C2B(sbi, 1),
			 EXT4_FREE_BLOCKS_RERESERVE_CLUSTER |
			 EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(12, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_freeclusters_counter),
			free_before);

	/*
	 * 3. Cross-group boundary free: 2 clusters at end of group 1 + 3 clusters
	 * at start of group 2.
	 */
	ext4_free_blocks(NULL, inode, NULL,
			 grp2_first - EXT4_C2B(sbi, 2),
			 EXT4_C2B(sbi, 5),
			 EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(max - 2, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(max - 1, mbt_ctx_bitmap(sb, 1)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(0, mbt_ctx_bitmap(sb, 2)), 0);
	KUNIT_EXPECT_EQ(test, mb_test_bit_test(2, mbt_ctx_bitmap(sb, 2)), 0);
}

static void test_mb_free_metadata_and_process_freed_data(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	struct ext4_buddy e4b;
	struct ext4_group_info *grp;
	struct ext4_free_data *entry;
	transaction_t trans = { .t_tid = 42 };
	handle_t handle = { .h_transaction = &trans };
	ext4_fsblk_t grp1_first = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	ext4_grpblk_t merged_start = 0, merged_count = 0;
	int node_count = 0, ret;
	struct rb_node *n;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);

	/* Mark clusters [20, 28) used in group 1 and load buddy */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 20, 8);
	grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	grp->bb_free -= 8;

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);

	/*
	 * Free [20, 22), then [25, 28), then the middle [22, 25) as metadata
	 * with a valid journal handle (tid = 42).
	 * This exercises rbtree insertion and both prev and next extent merging!
	 */
	ext4_free_blocks(&handle, inode, NULL,
			 grp1_first + EXT4_C2B(sbi, 20), EXT4_C2B(sbi, 2),
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
	ext4_free_blocks(&handle, inode, NULL,
			 grp1_first + EXT4_C2B(sbi, 25), EXT4_C2B(sbi, 3),
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
	ext4_free_blocks(&handle, inode, NULL,
			 grp1_first + EXT4_C2B(sbi, 22), EXT4_C2B(sbi, 3),
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);

	KUNIT_EXPECT_EQ(test, atomic_read(&sbi->s_mb_free_pending), 8);
	/* Blocks are not yet freed in buddy until transaction commit */
	KUNIT_EXPECT_EQ(test, grp->bb_free, EXT4_CLUSTERS_PER_GROUP(sb) - 8);

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	for (n = rb_first(&grp->bb_free_root); n; n = rb_next(n)) {
		entry = rb_entry(n, struct ext4_free_data, efd_node);
		merged_start = entry->efd_start_cluster;
		merged_count = entry->efd_count;
		node_count++;
	}
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	/* All 3 freed extents for tid 42 must have merged into a single node [20, 28) */
	KUNIT_EXPECT_EQ(test, node_count, 1);
	KUNIT_EXPECT_EQ(test, merged_start, 20);
	KUNIT_EXPECT_EQ(test, merged_count, 8);

	/* Commit tid 42 -> ext4_process_freed_data releases [20, 28) into buddy */
	ext4_process_freed_data(sb, 42);
	KUNIT_EXPECT_EQ(test, atomic_read(&sbi->s_mb_free_pending), 0);
	KUNIT_EXPECT_TRUE(test, RB_EMPTY_ROOT(&grp->bb_free_root));
	KUNIT_EXPECT_EQ(test, grp->bb_free, EXT4_CLUSTERS_PER_GROUP(sb));
}

static void test_ext4_group_add_blocks(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_buddy e4b;
	struct ext4_group_info *grp;
	ext4_fsblk_t grp1_first = ext4_group_first_block_no(sb, TEST_GOAL_GROUP);
	s64 free_before;
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	/* Mark [50, 60) used in group 1 and initialize buddy */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 50, 10);
	grp = ext4_get_group_info(sb, TEST_GOAL_GROUP);
	grp->bb_free -= 10;

	ret = ext4_mb_load_buddy_test(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_mb_unload_buddy_test(&e4b);

	free_before = percpu_counter_sum(&sbi->s_freeclusters_counter);
	ret = ext4_group_add_blocks(NULL, sb,
				    grp1_first + EXT4_C2B(sbi, 50),
				    EXT4_C2B(sbi, 10));
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, grp->bb_free, EXT4_CLUSTERS_PER_GROUP(sb));
	KUNIT_EXPECT_EQ(test, percpu_counter_sum(&sbi->s_freeclusters_counter),
			free_before + 10);

	/* Cross-group boundary addition must fail with -EINVAL */
	ret = ext4_group_add_blocks(NULL, sb,
				    grp1_first + EXT4_C2B(sbi, EXT4_CLUSTERS_PER_GROUP(sb) - 2),
				    EXT4_C2B(sbi, 5));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* System zone block (block 0) must fail with -EINVAL */
	ret = ext4_group_add_blocks(NULL, sb, 0, EXT4_C2B(sbi, 1));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static int mbt_discard_calls;
static int mbt_discard_clusters;

static int
ext4_issue_discard_count_stub(struct super_block *sb, ext4_group_t block_group,
			      ext4_grpblk_t cluster, int count)
{
	mbt_discard_calls++;
	mbt_discard_clusters += count;
	return 0;
}

static void test_ext4_trim_fs(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct fstrim_range range = {};
	struct ext4_group_info *grp1;
	struct inode *inode;
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	inode = mbt_alloc_test_inode(test, sb, S_IFREG);
	kunit_activate_static_stub(test, ext4_issue_discard,
				   ext4_issue_discard_count_stub);

	/*
	 * Mark groups 0, 2, 3 completely used.
	 * In group 1, leave only [10, 14) (4 clusters) and [30, 50) (20 clusters) free.
	 */
	mbt_ctx_mark_used(sb, 0, 0, max);
	ext4_get_group_info(sb, 0)->bb_free = 0;
	mbt_ctx_mark_used(sb, 2, 0, max);
	ext4_get_group_info(sb, 2)->bb_free = 0;
	mbt_ctx_mark_used(sb, 3, 0, max);
	ext4_get_group_info(sb, 3)->bb_free = 0;

	mbt_ctx_mark_used(sb, 1, 0, 10);
	mbt_ctx_mark_used(sb, 1, 14, 16);
	mbt_ctx_mark_used(sb, 1, 50, max - 50);
	grp1 = ext4_get_group_info(sb, 1);
	grp1->bb_free = 24;

	/* Invalid range checks */
	range.start = 0;
	range.len = sb->s_blocksize - 1;
	range.minlen = 0;
	KUNIT_EXPECT_EQ(test, ext4_trim_fs(sb, &range), -EINVAL);

	/* Trim with minlen = 10 clusters: skips [10, 14) and trims only [30, 50) (20 clusters) */
	mbt_discard_calls = 0;
	mbt_discard_clusters = 0;
	range.start = 0;
	range.len = (u64)ext4_blocks_count(sbi->s_es) << sb->s_blocksize_bits;
	range.minlen = (u64)EXT4_C2B(sbi, 10) << sb->s_blocksize_bits;

	ret = ext4_trim_fs(sb, &range);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, mbt_discard_calls, 1);
	KUNIT_EXPECT_EQ(test, mbt_discard_clusters, 20);
	KUNIT_EXPECT_EQ(test, range.len,
			(u64)EXT4_C2B(sbi, 20) << sb->s_blocksize_bits);
	KUNIT_EXPECT_TRUE(test, EXT4_MB_GRP_WAS_TRIMMED(grp1));

	/* Second trim with same minlen is skipped because WAS_TRIMMED is set */
	range.len = (u64)ext4_blocks_count(sbi->s_es) << sb->s_blocksize_bits;
	ret = ext4_trim_fs(sb, &range);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, mbt_discard_calls, 1);
	KUNIT_EXPECT_EQ(test, range.len, 0ULL);

	/* Freeing a block clears WAS_TRIMMED, allowing subsequent trim */
	ext4_free_blocks(NULL, inode, NULL,
			 ext4_group_first_block_no(sb, 1) + EXT4_C2B(sbi, 50),
			 EXT4_C2B(sbi, 1), EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
	KUNIT_EXPECT_FALSE(test, EXT4_MB_GRP_WAS_TRIMMED(grp1));
}

struct mbt_query_ctx {
	int meta_calls;
	ext4_grpblk_t meta_start;
	ext4_grpblk_t meta_len;
	int free_calls;
	ext4_grpblk_t free_starts[4];
	ext4_grpblk_t free_lens[4];
	int stop_after;
};

static int mbt_query_meta_cb(struct super_block *sb, ext4_group_t agno,
			     ext4_grpblk_t start, ext4_grpblk_t len, void *priv)
{
	struct mbt_query_ctx *q = priv;

	q->meta_calls++;
	q->meta_start = start;
	q->meta_len = len;
	return 0;
}

static int mbt_query_free_cb(struct super_block *sb, ext4_group_t agno,
			     ext4_grpblk_t start, ext4_grpblk_t len, void *priv)
{
	struct mbt_query_ctx *q = priv;

	if (q->free_calls < 4) {
		q->free_starts[q->free_calls] = start;
		q->free_lens[q->free_calls] = len;
	}
	q->free_calls++;
	if (q->stop_after && q->free_calls >= q->stop_after)
		return -ECANCELED;
	return 0;
}

static void test_ext4_mballoc_query_range(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct mbt_query_ctx q = {};
	ext4_grpblk_t max = EXT4_CLUSTERS_PER_GROUP(sb);
	int ret;

	if (sb->s_blocksize > PAGE_SIZE)
		kunit_skip(test, "blocksize exceeds pagesize");

	/*
	 * In group 1:
	 * [0, 10) used (before bb_first_free = 10 -> reported to meta_formatter)
	 * [10, 15) free (len 5)
	 * [15, 25) used
	 * [25, 32) free (len 7)
	 * [32, max) used
	 */
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 0, 10);
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 15, 10);
	mbt_ctx_mark_used(sb, TEST_GOAL_GROUP, 32, max - 32);
	ext4_get_group_info(sb, TEST_GOAL_GROUP)->bb_free = 12;

	ret = ext4_mballoc_query_range(sb, TEST_GOAL_GROUP, 0, 100,
				       mbt_query_meta_cb, mbt_query_free_cb, &q);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, q.meta_calls, 1);
	KUNIT_EXPECT_EQ(test, q.meta_start, 0);
	KUNIT_EXPECT_EQ(test, q.meta_len, 10);
	KUNIT_EXPECT_EQ(test, q.free_calls, 2);
	KUNIT_EXPECT_EQ(test, q.free_starts[0], 10);
	KUNIT_EXPECT_EQ(test, q.free_lens[0], 5);
	KUNIT_EXPECT_EQ(test, q.free_starts[1], 25);
	KUNIT_EXPECT_EQ(test, q.free_lens[1], 7);

	/* Early abort from formatter callback propagates error code */
	memset(&q, 0, sizeof(q));
	q.stop_after = 1;
	ret = ext4_mballoc_query_range(sb, TEST_GOAL_GROUP, 0, 100,
				       NULL, mbt_query_free_cb, &q);
	KUNIT_EXPECT_EQ(test, ret, -ECANCELED);
	KUNIT_EXPECT_EQ(test, q.free_calls, 1);
}

static const struct mbt_ext4_block_layout mbt_test_layouts[] = {
	{
		.blocksize_bits = 10,
		.cluster_bits = 3,
		.blocks_per_group = 8192,
		.group_count = 4,
		.desc_size = 64,
	},
	{
		.blocksize_bits = 12,
		.cluster_bits = 3,
		.blocks_per_group = 8192,
		.group_count = 4,
		.desc_size = 64,
	},
	{
		.blocksize_bits = 16,
		.cluster_bits = 3,
		.blocks_per_group = 8192,
		.group_count = 4,
		.desc_size = 64,
	},
};

static void mbt_show_layout(const struct mbt_ext4_block_layout *layout,
			    char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "block_bits=%d cluster_bits=%d "
		 "blocks_per_group=%d group_count=%d desc_size=%d\n",
		 layout->blocksize_bits, layout->cluster_bits,
		 layout->blocks_per_group, layout->group_count,
		 layout->desc_size);
}
KUNIT_ARRAY_PARAM(mbt_layouts, mbt_test_layouts, mbt_show_layout);

static struct kunit_case mbt_test_cases[] = {
	KUNIT_CASE_PARAM(test_new_blocks_simple, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_free_blocks_simple, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_generate_buddy, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_mark_used, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_free_blocks, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mark_diskspace_used, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_bit_ops, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_test_and_clear_bits, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_find_buddy_and_order, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_find_extent, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_generate_buddy_corrupt, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_free_blocks_double_free_and_fc_replay, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_xarray_orders_and_frag_size, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_good_group, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_find_by_goal, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_simple_and_aligned_scan, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_complex_scan_and_measure, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_regular_allocator, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_prefetch, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_initialize_context, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_normalize_request, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_inode_pa_lifecycle, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_group_pa_lifecycle, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_lg_list_trim, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_new_blocks_and_enospc_retry, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_mark_context_and_mark_bb, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_ext4_free_blocks_flags_and_cross_group, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_mb_free_metadata_and_process_freed_data, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_ext4_group_add_blocks, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_ext4_trim_fs, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM(test_ext4_mballoc_query_range, mbt_layouts_gen_params),
	KUNIT_CASE_PARAM_ATTR(test_mb_mark_used_cost, mbt_layouts_gen_params,
			      { .speed = KUNIT_SPEED_SLOW }),
	{}
};

static struct kunit_suite mbt_test_suite = {
	.name = "ext4_mballoc_test",
	.init = mbt_kunit_init,
	.exit = mbt_kunit_exit,
	.test_cases = mbt_test_cases,
};

kunit_test_suites(&mbt_test_suite);

MODULE_LICENSE("GPL");
