// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test of ext4 multiblocks allocation.
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>

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
	struct super_block sb;
	struct mbt_ctx mbt_ctx;
};

#define MBT_CTX(_sb) (&(container_of((_sb), struct mbt_ext4_super_block, sb)->mbt_ctx))
#define MBT_GRP_CTX(_sb, _group) (&MBT_CTX(_sb)->grp_ctx[_group])

static struct super_block *mbt_ext4_alloc_super_block(void)
{
	struct ext4_super_block *es = kzalloc(sizeof(*es), GFP_KERNEL);
	struct ext4_sb_info *sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	struct mbt_ext4_super_block *fsb = kzalloc(sizeof(*fsb), GFP_KERNEL);

	if (fsb == NULL || sbi == NULL || es == NULL)
		goto out;

	sbi->s_es = es;
	fsb->sb.s_fs_info = sbi;
	return &fsb->sb;

out:
	kfree(fsb);
	kfree(sbi);
	kfree(es);
	return NULL;
}

static void mbt_ext4_free_super_block(struct super_block *sb)
{
	struct mbt_ext4_super_block *fsb =
		container_of(sb, struct mbt_ext4_super_block, sb);
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	kfree(sbi->s_es);
	kfree(sbi);
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

	es->s_first_data_block = cpu_to_le32(0);
	es->s_blocks_count_lo = cpu_to_le32(layout->blocks_per_group *
					    layout->group_count);
}

static int mbt_grp_ctx_init(struct super_block *sb,
			    struct mbt_grp_ctx *grp_ctx)
{
	grp_ctx->bitmap_bh.b_data = kzalloc(EXT4_BLOCK_SIZE(sb), GFP_KERNEL);
	if (grp_ctx->bitmap_bh.b_data == NULL)
		return -ENOMEM;

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
	return 0;
}

static void mbt_kunit_exit(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;

	mbt_ctx_release(sb);
	mbt_ext4_free_super_block(sb);
}

static void test_new_blocks_simple(struct kunit *test)
{
	struct super_block *sb = (struct super_block *)test->priv;
	struct inode inode = { .i_sb = sb, };
	struct ext4_allocation_request ar;
	ext4_group_t i, goal_group = TEST_GOAL_GROUP;
	int err = 0;
	ext4_fsblk_t found;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	ar.inode = &inode;

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
