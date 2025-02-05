// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test of ext4 multiblocks allocation.
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/random.h>

#include "ext4.h"

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

	ei = kmalloc(sizeof(struct ext4_inode_info), GFP_KERNEL);
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

static const struct super_operations mbt_sops = {
	.alloc_inode	= mbt_alloc_inode,
	.free_inode	= mbt_free_inode,
};

static void mbt_kill_sb(struct super_block *sb)
{
	generic_shutdown_super(sb);
}

static struct file_system_type mbt_fs_type = {
	.name			= "mballoc test",
	.kill_sb		= mbt_kill_sb,
};

static int mbt_mb_init(struct super_block *sb)
{
	ext4_fsblk_t block;
	int ret;

	/* needed by ext4_mb_init->bdev_nonrot(sb->s_bdev) */
	sb->s_bdev = kzalloc(sizeof(*sb->s_bdev), GFP_KERNEL);
	if (sb->s_bdev == NULL)
		return -ENOMEM;

	sb->s_bdev->bd_queue = kzalloc(sizeof(struct request_queue), GFP_KERNEL);
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

static int mbt_set(struct super_block *sb, void *data)
{
	return 0;
}

static struct super_block *mbt_ext4_alloc_super_block(void)
{
	struct mbt_ext4_super_block *fsb;
	struct super_block *sb;
	struct ext4_sb_info *sbi;

	fsb = kzalloc(sizeof(*fsb), GFP_KERNEL);
	if (fsb == NULL)
		return NULL;

	sb = sget(&mbt_fs_type, NULL, mbt_set, 0, NULL);
	if (IS_ERR(sb))
		goto out;

	sbi = &fsb->sbi;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock)
		goto out_deactivate;

	bgl_lock_init(sbi->s_blockgroup_lock);

	sbi->s_es = &fsb->es;
	sb->s_fs_info = sbi;

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

	ctx->grp_ctx = kcalloc(ngroups, sizeof(struct mbt_grp_ctx),
			       GFP_KERNEL);
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

	if (state)
		mb_set_bits(bitmap_bh->b_data, blkoff, len);
	else
		mb_clear_bits(bitmap_bh->b_data, blkoff, len);

	return 0;
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

	test->priv = sb;
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

	/* stub function will be called in mbt_mb_init->ext4_mb_init */
	if (mbt_mb_init(sb) != 0) {
		mbt_ctx_release(sb);
		mbt_ext4_free_super_block(sb);
		return -ENOMEM;
	}

	return 0;
}

static void mbt_kunit_exit(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;

	mbt_mb_release(sb);
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

	inode = kunit_kzalloc(test, sizeof(*inode), GFP_KERNEL);
	if (!inode)
		return;

	inode->i_sb = sb;
	ar.inode = inode;

	/* get block at goal */
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test, ar.goal, found,
		"failed to alloc block at goal, expected %llu found %llu",
		ar.goal, found);

	/* get block after goal in goal group */
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test, ar.goal + EXT4_C2B(sbi, 1), found,
		"failed to alloc block after goal in goal group, expected %llu found %llu",
		ar.goal + 1, found);

	/* get block after goal group */
	mbt_ctx_mark_used(sb, goal_group, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test,
		ext4_group_first_block_no(sb, goal_group + 1), found,
		"failed to alloc block after goal group, expected %llu found %llu",
		ext4_group_first_block_no(sb, goal_group + 1), found);

	/* get block before goal group */
	for (i = goal_group; i < ext4_get_groups_count(sb); i++)
		mbt_ctx_mark_used(sb, i, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple(&ar, &err);
	KUNIT_ASSERT_EQ_MSG(test,
		ext4_group_first_block_no(sb, 0) + EXT4_C2B(sbi, 1), found,
		"failed to alloc block before goal group, expected %llu found %llu",
		ext4_group_first_block_no(sb, 0 + EXT4_C2B(sbi, 1)), found);

	/* no block available, fail to allocate block */
	for (i = 0; i < ext4_get_groups_count(sb); i++)
		mbt_ctx_mark_used(sb, i, 0, EXT4_CLUSTERS_PER_GROUP(sb));
	ar.goal = ext4_group_first_block_no(sb, goal_group);
	found = ext4_mb_new_blocks_simple(&ar, &err);
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
		bit = mb_find_next_zero_bit(bitmap, max, 0);
		KUNIT_ASSERT_EQ_MSG(test, bit, max,
				    "free block on unexpected group %d", i);
	}

	bitmap = mbt_ctx_bitmap(sb, goal_group);
	bit = mb_find_next_zero_bit(bitmap, max, 0);
	KUNIT_ASSERT_EQ(test, bit, start);

	bit = mb_find_next_bit(bitmap, max, bit + 1);
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

	inode = kunit_kzalloc(test, sizeof(*inode), GFP_KERNEL);
	if (!inode)
		return;
	inode->i_sb = sb;

	if (len == 0)
		return;

	block = ext4_group_first_block_no(sb, goal_group) +
		EXT4_C2B(sbi, start);
	ext4_free_blocks_simple(inode, block, len);
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
	ret = ext4_mb_mark_diskspace_used(ac, NULL, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	max = EXT4_CLUSTERS_PER_GROUP(sb);
	i = mb_find_next_bit(bitmap, max, 0);
	KUNIT_ASSERT_EQ(test, i, start);
	i = mb_find_next_zero_bit(bitmap, max, i + 1);
	KUNIT_ASSERT_EQ(test, i, start + len);
	i = mb_find_next_bit(bitmap, max, i + 1);
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

	inode = kunit_kzalloc(test, sizeof(*inode), GFP_KERNEL);
	if (!inode)
		return;
	inode->i_sb = sb;

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

	off = mb_find_next_zero_bit(bb, max, 0);
	grp->bb_first_free = off;
	while (off < max) {
		grp->bb_counters[0]++;
		grp->bb_free++;

		if (!(off & 1) && !mb_test_bit(off + 1, bb)) {
			grp->bb_free++;
			grp->bb_counters[0]--;
			mb_clear_bit(off >> 1, bb_h);
			grp->bb_counters[1]++;
			grp->bb_largest_free_order = 1;
			off++;
		}

		off = mb_find_next_zero_bit(bb, max, off + 1);
	}

	for (order = 1; order < MB_NUM_ORDERS(sb) - 1; order++) {
		bb = buddy + sbi->s_mb_offsets[order];
		bb_h = buddy + sbi->s_mb_offsets[order + 1];
		max = max >> 1;
		off = mb_find_next_zero_bit(bb, max, 0);

		while (off < max) {
			if (!(off & 1) && !mb_test_bit(off + 1, bb)) {
				mb_set_bits(bb, off, 2);
				grp->bb_counters[order] -= 2;
				mb_clear_bit(off >> 1, bb_h);
				grp->bb_counters[order + 1]++;
				grp->bb_largest_free_order = order + 1;
				off++;
			}

			off = mb_find_next_zero_bit(bb, max, off + 1);
		}
	}

	max = EXT4_CLUSTERS_PER_GROUP(sb);
	off = mb_find_next_zero_bit(bitmap, max, 0);
	while (off < max) {
		grp->bb_fragments++;

		off = mb_find_next_bit(bitmap, max, off + 1);
		if (off + 1 >= max)
			break;

		off = mb_find_next_zero_bit(bitmap, max, off + 1);
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
	ext4_mb_generate_buddy(sb, ext4_buddy, bitmap, TEST_GOAL_GROUP,
			       ext4_grp);

	KUNIT_ASSERT_EQ(test, memcmp(mbt_buddy, ext4_buddy, sb->s_blocksize),
			0);
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
	mb_mark_used(e4b, &ex);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	mb_set_bits(bitmap, start, len);
	/* bypass bb_free validatoin in ext4_mb_generate_buddy */
	grp->bb_free -= len;
	memset(buddy, 0xff, sb->s_blocksize);
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	ext4_mb_generate_buddy(sb, buddy, bitmap, 0, grp);

	KUNIT_ASSERT_EQ(test, memcmp(buddy, e4b->bd_buddy, sb->s_blocksize),
			0);
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

	ret = ext4_mb_load_buddy(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	grp->bb_free = EXT4_CLUSTERS_PER_GROUP(sb);
	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_mb_mark_used_range(test, &e4b, ranges[i].start,
					ranges[i].len, bitmap, buddy, grp);

	ext4_mb_unload_buddy(&e4b);
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
	mb_free_blocks(NULL, e4b, start, len);
	ext4_unlock_group(sb, e4b->bd_group);

	mb_clear_bits(bitmap, start, len);
	/* bypass bb_free validatoin in ext4_mb_generate_buddy */
	grp->bb_free += len;
	memset(buddy, 0xff, sb->s_blocksize);
	for (i = 0; i < MB_NUM_ORDERS(sb); i++)
		grp->bb_counters[i] = 0;
	ext4_mb_generate_buddy(sb, buddy, bitmap, 0, grp);

	KUNIT_ASSERT_EQ(test, memcmp(buddy, e4b->bd_buddy, sb->s_blocksize),
			0);
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

	ret = ext4_mb_load_buddy(sb, TEST_GOAL_GROUP, &e4b);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ex.fe_start = 0;
	ex.fe_len = EXT4_CLUSTERS_PER_GROUP(sb);
	ex.fe_group = TEST_GOAL_GROUP;

	ext4_lock_group(sb, TEST_GOAL_GROUP);
	mb_mark_used(&e4b, &ex);
	ext4_unlock_group(sb, TEST_GOAL_GROUP);

	grp->bb_free = 0;
	memset(bitmap, 0xff, sb->s_blocksize);

	mbt_generate_test_ranges(sb, ranges, TEST_RANGE_COUNT);
	for (i = 0; i < TEST_RANGE_COUNT; i++)
		test_mb_free_blocks_range(test, &e4b, ranges[i].start,
					  ranges[i].len, bitmap, buddy, grp);

	ext4_mb_unload_buddy(&e4b);
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

	ret = ext4_mb_load_buddy(sb, TEST_GOAL_GROUP, &e4b);
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
			mb_mark_used(&e4b, &ex);
			ext4_unlock_group(sb, TEST_GOAL_GROUP);
		}
		end = jiffies;
		all += (end - start);

		for (i = 0; i < TEST_RANGE_COUNT; i++) {
			if (ranges[i].len == 0)
				continue;

			ext4_lock_group(sb, TEST_GOAL_GROUP);
			mb_free_blocks(NULL, &e4b, ranges[i].start,
				       ranges[i].len);
			ext4_unlock_group(sb, TEST_GOAL_GROUP);
		}
	}

	kunit_info(test, "costed jiffies %lu\n", all);
	ext4_mb_unload_buddy(&e4b);
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
