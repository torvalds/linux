// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 extents status tree and bigalloc pending reservations.
 */

#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/static_stub.h>
#include <linux/fs_context.h>
#include <linux/seq_file.h>

#include "ext4.h"
#include "ext4_extents.h"

#define ES_MAX_EXPECTED_EXTENTS 8

struct es_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct ext4_inode_info *ei;

	/*
	 * Saved shrinker callbacks so the registered shrinker can be
	 * replaced with no-op stubs to prevent asynchronous background
	 * reclaim (kswapd / direct reclaim) from touching mock extents.
	 */
	unsigned long (*shrinker_count)(struct shrinker *shrink,
					struct shrink_control *sc);
	unsigned long (*shrinker_scan)(struct shrinker *shrink,
				       struct shrink_control *sc);

	/* Tracking for stubbed delalloc reservation callbacks */
	int da_update_calls;
	int last_da_used;
	int total_da_used;
	int last_da_quota_claim;

	int da_release_calls;
	int last_da_released;
	int total_da_released;
};

struct expected_es {
	ext4_lblk_t lblk;
	ext4_lblk_t len;
	ext4_fsblk_t pblk;
	unsigned int type;
};

static void reset_da_stub_stats(struct es_test_ctx *ctx)
{
	ctx->da_update_calls = 0;
	ctx->last_da_used = 0;
	ctx->total_da_used = 0;
	ctx->last_da_quota_claim = 0;

	ctx->da_release_calls = 0;
	ctx->last_da_released = 0;
	ctx->total_da_released = 0;
}

static void ext4_da_update_reserve_space_stub(struct inode *inode,
					      int used, int quota_claim)
{
	struct kunit *test = kunit_get_current_test();
	struct es_test_ctx *ctx;

	if (!test || !test->priv)
		return;

	ctx = test->priv;
	ctx->da_update_calls++;
	ctx->last_da_used = used;
	ctx->total_da_used += used;
	ctx->last_da_quota_claim = quota_claim;
}

static void ext4_da_release_space_stub(struct inode *inode, int to_free)
{
	struct kunit *test = kunit_get_current_test();
	struct es_test_ctx *ctx;

	if (!to_free || !test || !test->priv)
		return;

	ctx = test->priv;
	ctx->da_release_calls++;
	ctx->last_da_released = to_free;
	ctx->total_da_released += to_free;
}

static struct ext4_ext_path *
ext4_find_extent_stub(struct inode *inode, ext4_lblk_t block,
		      struct ext4_ext_path *path, int flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static unsigned long es_dummy_shrinker_count(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	return 0;
}

static unsigned long es_dummy_shrinker_scan(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	return SHRINK_STOP;
}

static unsigned long es_shrinker_count(struct es_test_ctx *ctx,
				       struct shrink_control *sc)
{
	return ctx->shrinker_count(ctx->sbi->s_es_shrinker, sc);
}

static unsigned long es_shrinker_scan(struct es_test_ctx *ctx,
				      struct shrink_control *sc)
{
	return ctx->shrinker_scan(ctx->sbi->s_es_shrinker, sc);
}

static void es_insert_extent(struct inode *inode, ext4_lblk_t lblk,
			     ext4_lblk_t len, ext4_fsblk_t pblk,
			     unsigned int status, bool delalloc_reserve_used)
{
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_insert_extent(inode, lblk, len, pblk, status,
			      delalloc_reserve_used);
	up_write(&EXT4_I(inode)->i_data_sem);
}

static void es_insert_delayed_extent(struct inode *inode, ext4_lblk_t lblk,
				     ext4_lblk_t len, bool lclu_allocated,
				     bool end_allocated)
{
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_insert_delayed_extent(inode, lblk, len, lclu_allocated,
				      end_allocated);
	up_write(&EXT4_I(inode)->i_data_sem);
}

static void es_cache_extent(struct inode *inode, ext4_lblk_t lblk,
			    ext4_lblk_t len, ext4_fsblk_t pblk,
			    unsigned int status)
{
	down_read(&EXT4_I(inode)->i_data_sem);
	ext4_es_cache_extent(inode, lblk, len, pblk, status);
	up_read(&EXT4_I(inode)->i_data_sem);
}

static void es_remove_extent(struct inode *inode, ext4_lblk_t lblk,
			     ext4_lblk_t len)
{
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_remove_extent(inode, lblk, len);
	up_write(&EXT4_I(inode)->i_data_sem);
}

static int es_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static int es_set_anon(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static struct file_system_type es_fs_type = {
	.name		 = "extents_status test",
	.init_fs_context = es_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void es_init_mock_inode(struct ext4_inode_info *ei,
			       struct super_block *sb, unsigned long ino)
{
	struct inode *inode = &ei->vfs_inode;

	memset(ei, 0, sizeof(*ei));
	inode->i_sb = sb;
	inode->i_ino = ino;
	inode->i_blkbits = sb->s_blocksize_bits;

	ext4_es_init_tree(&ei->i_es_tree);
	ext4_init_pending_tree(&ei->i_pending_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_list);
	init_rwsem(&ei->i_data_sem);
	spin_lock_init(&ei->i_block_reservation_lock);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
}

static void es_cleanup_inode(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct rb_node *node;

	sbi->s_mount_state &= ~EXT4_FC_REPLAY;
	es_remove_extent(inode, 0, EXT_MAX_BLOCKS);

	while ((node = rb_first(&ei->i_pending_tree.root)) != NULL) {
		struct pending_reservation *pr;

		pr = rb_entry(node, struct pending_reservation, rb_node);
		ext4_remove_pending(inode, EXT4_C2B(sbi, pr->lclu));
	}
}

KUNIT_DEFINE_ACTION_WRAPPER(es_cleanup_inode_action, es_cleanup_inode,
			    struct inode *);
static void es_cleanup_shrinker(struct ext4_sb_info *sbi)
{
	ext4_es_unregister_shrinker(sbi);
	ext4_es_destroy_stats(sbi);
}

KUNIT_DEFINE_ACTION_WRAPPER(es_unregister_shrinker_action,
			    es_cleanup_shrinker,
			    struct ext4_sb_info *);
KUNIT_DEFINE_ACTION_WRAPPER(es_deactivate_super_action, deactivate_super,
			    struct super_block *);

static struct inode *es_alloc_test_inode(struct kunit *test,
					 struct super_block *sb,
					 unsigned long ino)
{
	struct ext4_inode_info *ei;
	int ret;

	ei = kunit_kzalloc(test, sizeof(*ei), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ei);
	es_init_mock_inode(ei, sb, ino);

	ret = kunit_add_action_or_reset(test, es_cleanup_inode_action,
					&ei->vfs_inode);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return &ei->vfs_inode;
}

static void es_set_cluster_bits(struct super_block *sb,
				unsigned int cluster_bits)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	sbi->s_cluster_bits = cluster_bits;
	sbi->s_cluster_ratio = 1U << cluster_bits;
}

/*
 * All test resources and deferred cleanup actions are registered with KUnit's
 * resource stack (test->resources), which unwinds in strict LIFO order after
 * each test case completes (or aborts).
 *
 * The registration order below (and in helpers like es_alloc_test_inode())
 * MUST be preserved when modifying these tests so that teardown happens in the
 * reverse order:
 *   1. kunit_kzalloc() of ctx, sbi, and ctx->ei (freed last)
 *   2. es_deactivate_super_action(sb) (superblock deactivated after shrinker
 *      and all inodes are torn down)
 *   3. es_unregister_shrinker_action(sbi) (shrinker and s_es_stats per-CPU
 *      counters destroyed after all inode extent trees are emptied)
 *   4. kunit_activate_static_stub() for ext4_da_update_reserve_space,
 *      ext4_da_release_space, and ext4_find_extent (stubs must remain active
 *      while es_cleanup_inode() removes delayed extents via
 *      ext4_es_remove_extent())
 *   5. es_cleanup_inode_action() for ctx->ei and any additional test inodes
 *      allocated via es_alloc_test_inode() (run first during teardown; inside
 *      es_alloc_test_inode(), kunit_kzalloc() must precede
 *      kunit_add_action_or_reset() so the inode is cleaned up before freed)
 */
static int es_kunit_init(struct kunit *test)
{
	struct es_test_ctx *ctx;
	struct ext4_sb_info *sbi;
	struct super_block *sb;
	struct fs_context *fc;
	int err;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	ctx->ei = kunit_kzalloc(test, sizeof(*ctx->ei), GFP_KERNEL);
	if (!ctx->ei)
		return -ENOMEM;

	fc = fs_context_for_mount(&es_fs_type, 0);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	sb = sget_fc(fc, NULL, es_set_anon);
	put_fs_context(fc);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = 12;
	strscpy(sb->s_id, "es_kunit", sizeof(sb->s_id));

	es_set_cluster_bits(sb, 0);
	set_opt(sb, DELALLOC);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);
	up_write(&sb->s_umount);

	err = kunit_add_action_or_reset(test, es_deactivate_super_action, sb);
	if (err)
		return err;

	err = ext4_es_register_shrinker(sbi);
	if (err)
		return err;

	/*
	 * Detach the real callbacks from the globally registered shrinker
	 * before any extents are inserted so background kswapd / direct
	 * reclaim cannot asynchronously evict mock extents during a test.
	 */
	ctx->shrinker_count = sbi->s_es_shrinker->count_objects;
	ctx->shrinker_scan = sbi->s_es_shrinker->scan_objects;
	WRITE_ONCE(sbi->s_es_shrinker->count_objects, es_dummy_shrinker_count);
	WRITE_ONCE(sbi->s_es_shrinker->scan_objects, es_dummy_shrinker_scan);

	err = kunit_add_action_or_reset(test, es_unregister_shrinker_action,
					sbi);
	if (err)
		return err;

	ctx->sb = sb;
	ctx->sbi = sbi;
	test->priv = ctx;

	kunit_activate_static_stub(test, ext4_da_update_reserve_space,
				   ext4_da_update_reserve_space_stub);
	kunit_activate_static_stub(test, ext4_da_release_space,
				   ext4_da_release_space_stub);
	kunit_activate_static_stub(test, ext4_find_extent,
				   ext4_find_extent_stub);

	es_init_mock_inode(ctx->ei, sb, 12);
	err = kunit_add_action_or_reset(test, es_cleanup_inode_action,
					&ctx->ei->vfs_inode);
	if (err)
		return err;

	return 0;
}

static void assert_es_tree(struct kunit *test, struct inode *inode,
			   const struct expected_es *exp, unsigned int nr_exp)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct extent_status found[ES_MAX_EXPECTED_EXTENTS];
	struct rb_node *node;
	unsigned int nr_found = 0;
	unsigned int all_nr;
	unsigned int i;
	ext4_lblk_t prev_end = 0;

	KUNIT_ASSERT_LE(test, nr_exp, (unsigned int)ARRAY_SIZE(found));

	/*
	 * Snapshot the tree under i_es_lock and release the lock before
	 * running any KUnit assertions or expectations. KUNIT_ASSERT_*
	 * aborts the test kthread on failure (which would leak i_es_lock
	 * and deadlock es_cleanup_inode()), and KUNIT_EXPECT_* allocates
	 * failure logs with GFP_KERNEL.
	 */
	read_lock(&ei->i_es_lock);
	for (node = rb_first(&ei->i_es_tree.root); node; node = rb_next(node)) {
		struct extent_status *es =
			rb_entry(node, struct extent_status, rb_node);

		if (nr_found < ARRAY_SIZE(found))
			found[nr_found] = *es;
		nr_found++;
	}
	all_nr = ei->i_es_all_nr;
	read_unlock(&ei->i_es_lock);

	for (i = 0; i < nr_found; i++) {
		struct extent_status *es = &found[min(i,
				(unsigned int)ARRAY_SIZE(found) - 1)];

		KUNIT_ASSERT_LT_MSG(test, i, nr_exp,
				    "unexpected extra extent [%u/%u) type 0x%x",
				    es->es_lblk, es->es_len, ext4_es_type(es));

		if (i > 0)
			KUNIT_EXPECT_GT_MSG(test, es->es_lblk, prev_end,
					    "extent %u not strictly ordered",
					    i);

		KUNIT_EXPECT_EQ_MSG(test, es->es_lblk, exp[i].lblk,
				    "extent %u lblk mismatch", i);
		KUNIT_EXPECT_EQ_MSG(test, es->es_len, exp[i].len,
				    "extent %u len mismatch", i);
		KUNIT_EXPECT_EQ_MSG(test, ext4_es_type(es), exp[i].type,
				    "extent %u type mismatch", i);

		if (exp[i].type & (EXTENT_STATUS_WRITTEN |
				   EXTENT_STATUS_UNWRITTEN))
			KUNIT_EXPECT_EQ_MSG(test, ext4_es_pblock(es),
					    exp[i].pblk,
					    "extent %u pblk mismatch", i);

		prev_end = es->es_lblk + es->es_len - 1;
	}

	KUNIT_EXPECT_EQ(test, nr_found, nr_exp);
	KUNIT_EXPECT_EQ(test, all_nr, nr_exp);
}

/*
 * Group 1: Bitfield Encoding & Inline Helpers
 */
static void test_es_bitfield_written_and_unwritten_encoding(struct kunit *test)
{
	struct extent_status es = {};
	const ext4_fsblk_t sample_pblk = 0x123456789abcULL;

	ext4_es_store_pblock_status(&es, sample_pblk, EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), sample_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_show_pblock(&es), sample_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_type(&es), EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_EQ(test, ext4_es_status(&es), EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_written(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_unwritten(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_delayed(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_hole(&es));
	KUNIT_EXPECT_TRUE(test, ext4_es_is_mapped(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_referenced(&es));

	ext4_es_store_pblock_status(&es, sample_pblk, EXTENT_STATUS_UNWRITTEN);
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), sample_pblk);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_unwritten(&es));
	KUNIT_EXPECT_TRUE(test, ext4_es_is_mapped(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_written(&es));
}

static void test_es_bitfield_delayed_and_hole_encoding(struct kunit *test)
{
	struct extent_status es = {};
	/* 59-bit physical block field (64 - 5 status bits) all ones */
	const ext4_fsblk_t max_59bit_pblk = 0x07ffffffffffffffULL;

	ext4_es_store_pblock_status(&es, ~0ULL, EXTENT_STATUS_DELAYED);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_delayed(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_mapped(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), max_59bit_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_show_pblock(&es), 0ULL);

	ext4_es_store_pblock_status(&es, 0ULL, EXTENT_STATUS_HOLE);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_hole(&es));
	KUNIT_EXPECT_FALSE(test, ext4_es_is_mapped(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 0ULL);
}

static void
test_es_bitfield_referenced_and_pblock_update_preserve_state(struct kunit *test)
{
	struct extent_status es = {};
	const ext4_fsblk_t initial_pblk = 0x123456789abcULL;
	const ext4_fsblk_t updated_pblk = 0x07ffffffffffffffULL;

	ext4_es_store_pblock_status(&es, initial_pblk, EXTENT_STATUS_WRITTEN);

	/* Setting referenced bit preserves pblk and type */
	ext4_es_set_referenced(&es);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_referenced(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_type(&es), EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), initial_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_status(&es),
			EXTENT_STATUS_WRITTEN | EXTENT_STATUS_REFERENCED);

	/* Updating pblock preserves status and referenced bit */
	ext4_es_store_pblock(&es, updated_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), updated_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_type(&es), EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_referenced(&es));

	/* Clearing referenced bit preserves updated pblk and type */
	ext4_es_clear_referenced(&es);
	KUNIT_EXPECT_FALSE(test, ext4_es_is_referenced(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), updated_pblk);
	KUNIT_EXPECT_EQ(test, ext4_es_type(&es), EXTENT_STATUS_WRITTEN);
}

/*
 * Group 2: Insertion, Lookup, Sequence Numbers, and Merging
 */
static void test_es_lookup_empty_tree_returns_miss(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct extent_status es;
	ext4_lblk_t next_lblk = 0xdead;
	s64 misses_before;
	int ret;

	misses_before = percpu_counter_sum_positive(
		&ctx->sbi->s_es_stats.es_stats_cache_misses);
	ret = ext4_es_lookup_extent(inode, 10, &next_lblk, &es, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, percpu_counter_sum_positive(
		&ctx->sbi->s_es_stats.es_stats_cache_misses),
		misses_before + 1);
}

static void test_es_insert_zero_len_is_noop(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	es_insert_extent(inode, 10, 0, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_delayed_extent(inode, 20, 0, false, false);
	es_cache_extent(inode, 30, 0, 300, EXTENT_STATUS_WRITTEN);

	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_all_nr, 0U);
	KUNIT_EXPECT_TRUE(test, RB_EMPTY_ROOT(&ctx->ei->i_es_tree.root));
}

static void
test_es_lookup_hits_cache_and_rbtree_with_next_lblk(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct extent_status es;
	ext4_lblk_t next_lblk = 0xdead;
	u64 seq_after_first, seq_after_second, lookup_seq = 0;
	s64 hits_before;
	int ret;

	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);
	seq_after_first = ctx->ei->i_es_seq;
	es_insert_extent(inode, 20, 5, 200, EXTENT_STATUS_UNWRITTEN, false);
	seq_after_second = ctx->ei->i_es_seq;
	KUNIT_EXPECT_GT(test, seq_after_second, seq_after_first);

	/*
	 * Right after inserting [20, 5), tree->cache_es points to [20, 5).
	 * Looking up lblk 22 hits cache_es and sets next_lblk to 0 because
	 * [20, 5) is the last extent in the tree.
	 */
	hits_before = percpu_counter_sum_positive(
		&ctx->sbi->s_es_stats.es_stats_cache_hits);

	ret = ext4_es_lookup_extent(inode, 22, &next_lblk, &es, &lookup_seq);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 20U);
	KUNIT_EXPECT_EQ(test, es.es_len, 5U);
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 200ULL);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_unwritten(&es));
	KUNIT_EXPECT_EQ(test, next_lblk, 0U);
	KUNIT_EXPECT_EQ(test, lookup_seq, seq_after_second);

	/* Lookup start, middle, and end of first extent [10, 5) via rb-tree */
	ret = ext4_es_lookup_extent(inode, 10, &next_lblk, &es, NULL);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 10U);
	KUNIT_EXPECT_EQ(test, next_lblk, 20U);

	ret = ext4_es_lookup_extent(inode, 12, &next_lblk, &es, NULL);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 10U);
	KUNIT_EXPECT_EQ(test, next_lblk, 20U);

	ret = ext4_es_lookup_extent(inode, 14, &next_lblk, &es, NULL);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 10U);
	KUNIT_EXPECT_EQ(test, next_lblk, 20U);

	KUNIT_EXPECT_EQ(test, percpu_counter_sum_positive(
		&ctx->sbi->s_es_stats.es_stats_cache_hits),
		hits_before + 4);

	/* Lookup in gap [15..19] returns 0 */
	ret = ext4_es_lookup_extent(inode, 15, &next_lblk, &es, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void test_es_fc_replay_bypasses_tree_operations(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct extent_status es;
	static const struct expected_es expected[] = {
		{ .lblk = 10, .len = 5, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
	};

	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);

	ctx->sbi->s_mount_state |= EXT4_FC_REPLAY;
	KUNIT_EXPECT_EQ(test,
			ext4_es_lookup_extent(inode, 10, NULL, &es, NULL), 0);
	ext4_es_find_extent_range(inode, ext4_es_is_written, 0, 20, &es);
	KUNIT_EXPECT_EQ(test, es.es_len, 0U);
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_range(inode, ext4_es_is_written,
					      0, 20));
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_clu(inode, ext4_es_is_written, 10));

	es_insert_extent(inode, 30, 5, 300, EXTENT_STATUS_WRITTEN, false);
	es_cache_extent(inode, 40, 5, 400, EXTENT_STATUS_WRITTEN);
	es_insert_delayed_extent(inode, 50, 5, false, false);
	es_remove_extent(inode, 10, 5);
	ctx->sbi->s_mount_state &= ~EXT4_FC_REPLAY;

	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

struct es_merge_test_param {
	const char *desc;
	unsigned int nr_inserts;
	struct expected_es inserts[3];
	unsigned int nr_expected;
	struct expected_es expected[3];
};

static const struct es_merge_test_param es_merge_params[] = {
	{
		.desc = "left merge contiguous written extents",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_WRITTEN },
		},
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 10, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "right merge contiguous written extents",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 10, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "three-way bridge merge via try_to_merge_right",
		.nr_inserts = 3,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 20, .len = 5, .pblk = 110,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_WRITTEN },
		},
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 15, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "three-way bridge merge via try_to_merge_left",
		.nr_inserts = 3,
		.inserts = {
			{ .lblk = 20, .len = 5, .pblk = 110,
			  .type = EXTENT_STATUS_UNWRITTEN },
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_UNWRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_UNWRITTEN },
		},
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 15, .pblk = 100,
			  .type = EXTENT_STATUS_UNWRITTEN },
		},
	},
	{
		.desc = "adjacent hole extents merge",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 0,
			  .type = EXTENT_STATUS_HOLE },
			{ .lblk = 15, .len = 5, .pblk = 0,
			  .type = EXTENT_STATUS_HOLE },
		},
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 10, .pblk = 0,
			  .type = EXTENT_STATUS_HOLE },
		},
	},
	{
		.desc = "discontiguous physical blocks do not merge",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 200,
			  .type = EXTENT_STATUS_WRITTEN },
		},
		.nr_expected = 2,
		.expected = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 200,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "different status types with contiguous pblk do not merge",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_UNWRITTEN },
		},
		.nr_expected = 2,
		.expected = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 15, .len = 5, .pblk = 105,
			  .type = EXTENT_STATUS_UNWRITTEN },
		},
	},
	{
		.desc = "logically separated extents with 1-block gap do not merge",
		.nr_inserts = 2,
		.inserts = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 16, .len = 5, .pblk = 106,
			  .type = EXTENT_STATUS_WRITTEN },
		},
		.nr_expected = 2,
		.expected = {
			{ .lblk = 10, .len = 5, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 16, .len = 5, .pblk = 106,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
};

static void es_merge_get_desc(const struct es_merge_test_param *param,
			      char *desc)
{
	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}
KUNIT_ARRAY_PARAM(es_merge, es_merge_params, es_merge_get_desc);

static void test_es_merge_cases(struct kunit *test)
{
	const struct es_merge_test_param *param = test->param_value;
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	unsigned int i;

	for (i = 0; i < param->nr_inserts; i++) {
		const struct expected_es *ins = &param->inserts[i];

		es_insert_extent(inode, ins->lblk, ins->len,
				 ins->pblk, ins->type, false);
	}

	assert_es_tree(test, inode, param->expected, param->nr_expected);
}

static void test_es_insert_adjacent_delayed_extents_merges(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 10, .len = 10, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};

	es_insert_delayed_extent(inode, 10, 5, false, false);
	es_insert_delayed_extent(inode, 15, 5, false, false);

	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

static void test_es_merge_preserves_referenced(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct extent_status es;
	struct rb_node *node;
	bool is_ref = false;

	/*
	 * Insert [10, 5) and [20, 5), mark [20, 5) referenced via lookup,
	 * then bridge [15, 5) so [20, 5) is merged into [10, 10) via
	 * ext4_es_try_to_merge_right().
	 */
	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 20, 5, 110, EXTENT_STATUS_WRITTEN, false);
	KUNIT_ASSERT_EQ(test,
			ext4_es_lookup_extent(inode, 20, NULL, &es, NULL), 1);

	es_insert_extent(inode, 15, 5, 105, EXTENT_STATUS_WRITTEN, false);
	KUNIT_ASSERT_EQ(test, ctx->ei->i_es_all_nr, 1U);

	read_lock(&ctx->ei->i_es_lock);
	node = rb_first(&ctx->ei->i_es_tree.root);
	if (node)
		is_ref = ext4_es_is_referenced(rb_entry(node,
							struct extent_status,
							rb_node));
	read_unlock(&ctx->ei->i_es_lock);

	KUNIT_ASSERT_NOT_NULL(test, node);
	KUNIT_EXPECT_TRUE(test, is_ref);
}

static void
test_es_merge_at_max_blocks_boundary_succeeds(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 0, .len = EXT_MAX_BLOCKS, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
	};

	es_insert_extent(inode, 0, 0x80000000U, 100,
			 EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 0x80000000U, 0x7fffffffU,
			 100ULL + 0x80000000ULL,
			 EXTENT_STATUS_WRITTEN, false);

	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

/*
 * Group 3: Removal, Trimming, Splitting, and Overwrites
 */
struct es_remove_test_param {
	const char *desc;
	ext4_lblk_t rem_lblk;
	ext4_lblk_t rem_len;
	unsigned int nr_expected;
	struct expected_es expected[2];
};

static const struct es_remove_test_param es_remove_params[] = {
	{
		.desc = "remove range before extent",
		.rem_lblk = 0,
		.rem_len = 5,
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 10, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "remove range after extent",
		.rem_lblk = 20,
		.rem_len = 5,
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 10, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "remove exact extent range",
		.rem_lblk = 10,
		.rem_len = 10,
		.nr_expected = 0,
	},
	{
		.desc = "remove superset of extent range",
		.rem_lblk = 5,
		.rem_len = 20,
		.nr_expected = 0,
	},
	{
		.desc = "prefix trim of extent",
		.rem_lblk = 10,
		.rem_len = 4,
		.nr_expected = 1,
		.expected = {
			{ .lblk = 14, .len = 6, .pblk = 104,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "suffix trim of extent",
		.rem_lblk = 16,
		.rem_len = 4,
		.nr_expected = 1,
		.expected = {
			{ .lblk = 10, .len = 6, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
	{
		.desc = "middle split of extent into two",
		.rem_lblk = 13,
		.rem_len = 4,
		.nr_expected = 2,
		.expected = {
			{ .lblk = 10, .len = 3, .pblk = 100,
			  .type = EXTENT_STATUS_WRITTEN },
			{ .lblk = 17, .len = 3, .pblk = 107,
			  .type = EXTENT_STATUS_WRITTEN },
		},
	},
};

static void es_remove_get_desc(const struct es_remove_test_param *param,
			       char *desc)
{
	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}
KUNIT_ARRAY_PARAM(es_remove, es_remove_params, es_remove_get_desc);

static void test_es_remove_geometries(struct kunit *test)
{
	const struct es_remove_test_param *param = test->param_value;
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	es_insert_extent(inode, 10, 10, 100, EXTENT_STATUS_WRITTEN, false);
	es_remove_extent(inode, param->rem_lblk, param->rem_len);

	assert_es_tree(test, inode, param->expected, param->nr_expected);
}

static void
test_es_remove_multi_extent_span_trims_and_deletes(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 10, .len = 2, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
		{ .lblk = 43, .len = 2, .pblk = 403,
		  .type = EXTENT_STATUS_UNWRITTEN },
	};

	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 20, 5, 200, EXTENT_STATUS_HOLE, false);
	es_insert_extent(inode, 30, 5, 300, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 40, 5, 400, EXTENT_STATUS_UNWRITTEN, false);

	/*
	 * Remove [12, 31) covering lblks 12..42 inclusive:
	 * - trims suffix of [10, 5) -> [10, 2)
	 * - completely removes [20, 5) and [30, 5)
	 * - trims prefix of [40, 5) -> [43, 2)
	 */
	es_remove_extent(inode, 12, 31);
	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

static void test_es_insert_overwrite_split_and_remerge(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es split_exp[] = {
		{ .lblk = 10, .len = 3, .pblk = 100,
		  .type = EXTENT_STATUS_UNWRITTEN },
		{ .lblk = 13, .len = 4, .pblk = 103,
		  .type = EXTENT_STATUS_WRITTEN },
		{ .lblk = 17, .len = 3, .pblk = 107,
		  .type = EXTENT_STATUS_UNWRITTEN },
	};
	static const struct expected_es merged_exp[] = {
		{ .lblk = 10, .len = 10, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
	};

	es_insert_extent(inode, 10, 10, 100, EXTENT_STATUS_UNWRITTEN, false);
	es_insert_extent(inode, 13, 4, 103, EXTENT_STATUS_WRITTEN, false);
	assert_es_tree(test, inode, split_exp, ARRAY_SIZE(split_exp));

	/* Convert the remaining prefix and suffix to WRITTEN; all 3 merge */
	es_insert_extent(inode, 10, 3, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 17, 3, 107, EXTENT_STATUS_WRITTEN, false);
	assert_es_tree(test, inode, merged_exp, ARRAY_SIZE(merged_exp));
}

/*
 * Group 4: On-Disk Caching & Conflict Detection (ext4_es_cache_extent)
 */
static void
test_es_cache_extent_inserts_and_coalesces_same_status(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es initial_exp[] = {
		{ .lblk = 10, .len = 5, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
	};
	static const struct expected_es coalesced_exp[] = {
		{ .lblk = 8, .len = 10, .pblk = 98,
		  .type = EXTENT_STATUS_WRITTEN },
	};
	int warnings_before;

	warnings_before = atomic_read(&ctx->sbi->s_warning_count);

	/* Cache into empty tree */
	es_cache_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN);
	assert_es_tree(test, inode, initial_exp, ARRAY_SIZE(initial_exp));

	/* Caching sub-range with identical status is a no-op */
	es_cache_extent(inode, 11, 2, 101, EXTENT_STATUS_WRITTEN);
	assert_es_tree(test, inode, initial_exp, ARRAY_SIZE(initial_exp));

	/* Caching larger covering range with same status coalesces */
	es_cache_extent(inode, 8, 10, 98, EXTENT_STATUS_WRITTEN);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->sbi->s_warning_count),
			warnings_before);
	assert_es_tree(test, inode, coalesced_exp, ARRAY_SIZE(coalesced_exp));
}

static void
test_es_cache_extent_conflict_covered_range_warns(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 8, .len = 10, .pblk = 98,
		  .type = EXTENT_STATUS_WRITTEN },
	};
	int warnings_before;

	es_cache_extent(inode, 8, 10, 98, EXTENT_STATUS_WRITTEN);
	warnings_before = atomic_read(&ctx->sbi->s_warning_count);

	/* Sub-range [10, 4) covered by [8, 10) with conflicting status */
	es_cache_extent(inode, 10, 4, 500, EXTENT_STATUS_UNWRITTEN);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->sbi->s_warning_count),
			warnings_before + 1);
	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

static void
test_es_cache_extent_conflict_partial_overlap_warns(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 10, .len = 5, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
	};
	int warnings_before;

	es_cache_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN);
	warnings_before = atomic_read(&ctx->sbi->s_warning_count);

	/*
	 * Range [8, 10) is not covered by [10, 5), so ext4_es_cache_extent()
	 * enters __es_remove_extent() which detects the status conflict and
	 * returns -EINVAL without modifying [10, 5).
	 */
	es_cache_extent(inode, 8, 10, 500, EXTENT_STATUS_UNWRITTEN);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->sbi->s_warning_count),
			warnings_before + 1);
	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

static void
test_es_cache_extent_hole_over_delayed_does_not_warn(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es expected[] = {
		{ .lblk = 30, .len = 5, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};
	int warnings_before;

	es_insert_delayed_extent(inode, 30, 5, false, false);
	warnings_before = atomic_read(&ctx->sbi->s_warning_count);

	es_cache_extent(inode, 30, 5, 0, EXTENT_STATUS_HOLE);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->sbi->s_warning_count),
			warnings_before);
	assert_es_tree(test, inode, expected, ARRAY_SIZE(expected));
}

/*
 * Group 5: Range Search & Cluster Scanning
 */
static void
test_es_find_extent_range_skips_nonmatching_and_finds_next(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct extent_status es;

	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_delayed_extent(inode, 20, 5, false, false);
	es_insert_extent(inode, 30, 5, 300, EXTENT_STATUS_UNWRITTEN, false);

	/*
	 * Prime cache_es to [10, 5) WRITTEN, then search [10, 25] for DELAYED:
	 * cache_es is hit at lblk 10, fails matching_fn(ext4_es_is_delayed),
	 * and walks rb_next to find [20, 5) DELAYED.
	 */
	KUNIT_ASSERT_EQ(test,
			ext4_es_lookup_extent(inode, 10, NULL, &es, NULL), 1);
	ext4_es_find_extent_range(inode, ext4_es_is_delayed, 10, 25, &es);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 20U);
	KUNIT_EXPECT_EQ(test, es.es_len, 5U);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_delayed(&es));

	/* Search [10, 18] for DELAYED when starting inside [10, 5) returns 0 */
	KUNIT_ASSERT_EQ(test,
			ext4_es_lookup_extent(inode, 10, NULL, &es, NULL), 1);
	ext4_es_find_extent_range(inode, ext4_es_is_delayed, 10, 18, &es);
	KUNIT_EXPECT_EQ(test, es.es_len, 0U);

	/*
	 * Search [16, 18] in gap before [20, 5): __es_tree_search(16) returns
	 * the next extent [20, 5) following the range.
	 */
	ext4_es_find_extent_range(inode, ext4_es_is_delayed, 16, 18, &es);
	KUNIT_EXPECT_EQ(test, es.es_lblk, 20U);
	KUNIT_EXPECT_EQ(test, es.es_len, 5U);
}

static void
test_es_scan_range_matches_only_overlapping_extents(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	es_insert_delayed_extent(inode, 20, 5, false, false);
	es_insert_extent(inode, 30, 5, 300, EXTENT_STATUS_UNWRITTEN, false);

	/* Gap [16, 18] before [20, 5) does not overlap */
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_range(inode, ext4_es_is_delayed,
					      16, 18));

	/* Ranges overlapping [20, 5) or [30, 5) return true */
	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_range(inode, ext4_es_is_delayed,
					     16, 20));
	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_range(inode, ext4_es_is_delayed,
					     22, 23));
	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_range(inode, ext4_es_is_mapped,
					     34, 40));

	/* Range [35, 40] after [30, 5) does not overlap */
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_range(inode, ext4_es_is_mapped,
					      35, 40));
}

static void test_es_scan_clu_matches_extent_in_same_cluster(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Configure cluster size = 16 blocks (cluster_bits = 4) */
	es_set_cluster_bits(ctx->sb, 4);

	/* Insert delayed block at the very end of cluster 0 (lblk 15) */
	es_insert_delayed_extent(inode, 15, 1, false, false);

	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_clu(inode, ext4_es_is_delayed, 0));
	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_clu(inode, ext4_es_is_delayed, 8));
	KUNIT_EXPECT_TRUE(test,
			  ext4_es_scan_clu(inode, ext4_es_is_delayed, 15));
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_clu(inode, ext4_es_is_delayed, 16));
	KUNIT_EXPECT_FALSE(test,
			   ext4_es_scan_clu(inode, ext4_es_is_mapped, 0));
}

/*
 * Group 6: Delayed Allocation & Bigalloc Pending Reservations
 */
static void
test_es_delalloc_remove_and_overwrite_account_reserved_blocks(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es after_rem_exp[] = {
		{ .lblk = 10, .len = 2, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
		{ .lblk = 15, .len = 5, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};
	static const struct expected_es after_ins_exp[] = {
		{ .lblk = 10, .len = 6, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
		{ .lblk = 16, .len = 4, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};

	/* Delayed extents are must-keep: i_es_shk_nr remains 0 */
	es_insert_delayed_extent(inode, 10, 10, false, false);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_all_nr, 1U);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei->i_es_list));

	/* Removing 3 delayed blocks [12, 3) releases 3 reserved blocks */
	reset_da_stub_stats(ctx);
	es_remove_extent(inode, 12, 3);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_released, 3);
	assert_es_tree(test, inode, after_rem_exp, ARRAY_SIZE(after_rem_exp));

	/*
	 * Inserting WRITTEN [10, 6) overwrites [10, 2) delayed, [12, 3) gap,
	 * and [15, 1) delayed -> claims 3 delayed blocks of reservation.
	 */
	reset_da_stub_stats(ctx);
	es_insert_extent(inode, 10, 6, 100, EXTENT_STATUS_WRITTEN, true);
	KUNIT_EXPECT_EQ(test, ctx->da_update_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_used, 3);
	KUNIT_EXPECT_EQ(test, ctx->last_da_quota_claim, 1);
	assert_es_tree(test, inode, after_ins_exp, ARRAY_SIZE(after_ins_exp));
}

static void
test_es_bigalloc_insert_delayed_creates_and_removes_pending(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks: clu 0=[0..15], clu 1=[16..31], clu 2=[32..47] */
	es_set_cluster_bits(ctx->sb, 4);

	/* Single cluster with lclu_allocated = true */
	es_insert_delayed_extent(inode, 2, 4, true, false);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 0));
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 15));
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 16));

	/* Multi-cluster [20, 20) spanning clusters 1 and 2, both allocated */
	es_insert_delayed_extent(inode, 20, 20, true, true);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 16));
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 32));
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 48));

	/* Duplicate pending insertion on cluster 0 is idempotent */
	es_insert_delayed_extent(inode, 8, 2, true, false);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 0));

	/* Explicit removal of pending reservation */
	ext4_remove_pending(inode, 16);
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 16));
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 32));
}

static void
test_es_bigalloc_revise_pending_single_cluster_halves(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks */
	es_set_cluster_bits(ctx->sb, 4);

	/* Insert delayed extent [0, 32) covering clusters 0 and 1 */
	es_insert_delayed_extent(inode, 0, 32, false, false);

	/*
	 * 1. Allocate first half of cluster 0: [0, 8) -> WRITTEN.
	 * Delayed blocks [8, 8) remain after [0, 8) in cluster 0 (l_del=true):
	 * __revise_pending inserts pending on cluster 0 and claims 1 cluster.
	 */
	reset_da_stub_stats(ctx);
	es_insert_extent(inode, 0, 8, 100, EXTENT_STATUS_WRITTEN, true);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 0));
	KUNIT_EXPECT_EQ(test, ctx->da_update_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_used, 1);

	/*
	 * 2. Allocate second half of cluster 0: [8, 8) -> WRITTEN.
	 * No delayed blocks remain in cluster 0; pending reservation on
	 * cluster 0 is removed and 0 additional clusters are claimed.
	 */
	reset_da_stub_stats(ctx);
	es_insert_extent(inode, 8, 8, 108, EXTENT_STATUS_WRITTEN, true);
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 0));
	KUNIT_EXPECT_EQ(test, ctx->da_update_calls, 0);

	/*
	 * 3. Allocate second half of cluster 1 first: [24, 8) -> WRITTEN while
	 * [16, 8) remains delayed before it in cluster 1 (f_del=true branch in
	 * __revise_pending): inserts pending on cluster 1 and claims 1 cluster.
	 */
	reset_da_stub_stats(ctx);
	es_insert_extent(inode, 24, 8, 224, EXTENT_STATUS_WRITTEN, true);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 16));
	KUNIT_EXPECT_EQ(test, ctx->da_update_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_used, 1);
}

static void
test_es_bigalloc_revise_pending_multi_cluster_span(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks */
	es_set_cluster_bits(ctx->sb, 4);

	/*
	 * Insert delayed [16, 32) across cluster 1 [16..31] and cluster 2
	 * [32..47], then allocate [20, 24) (lblks 20..43), leaving delayed
	 * [16, 4) in cluster 1 and delayed [44, 4) in cluster 2. Both clusters
	 * get pending reservations and 2 clusters are claimed.
	 */
	es_insert_delayed_extent(inode, 16, 32, false, false);
	reset_da_stub_stats(ctx);
	es_insert_extent(inode, 20, 24, 220, EXTENT_STATUS_WRITTEN, true);
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 16));
	KUNIT_EXPECT_TRUE(test, ext4_is_pending(inode, 32));
	KUNIT_EXPECT_EQ(test, ctx->da_update_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_used, 2);
}

static void
test_es_bigalloc_remove_partial_cluster_holds_rsvd_until_empty(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks */
	es_set_cluster_bits(ctx->sb, 4);

	es_insert_delayed_extent(inode, 0, 16, false, false);
	reset_da_stub_stats(ctx);

	es_remove_extent(inode, 0, 4);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 0);

	es_remove_extent(inode, 12, 4);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 0);

	es_remove_extent(inode, 4, 8);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_released, 1);
}

static void
test_es_bigalloc_remove_fragmented_cluster_releases_once(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks */
	es_set_cluster_bits(ctx->sb, 4);

	/*
	 * Two fragmented delayed extents within cluster 0 ([2, 3) and [10, 3))
	 * share a single cluster reservation and must release 1 cluster, not 2.
	 */
	es_insert_delayed_extent(inode, 2, 3, false, false);
	es_insert_delayed_extent(inode, 10, 3, false, false);
	reset_da_stub_stats(ctx);

	es_remove_extent(inode, 0, 16);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_da_released, 1);
}

static void
test_es_bigalloc_remove_pending_cluster_cancels_pending_only(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;

	/* Cluster size = 16 blocks */
	es_set_cluster_bits(ctx->sb, 4);

	/*
	 * Removing a delayed extent in a cluster with a pending reservation
	 * cancels the pending reservation and releases 0 clusters (because the
	 * cluster reservation was already claimed when allocated).
	 */
	es_insert_delayed_extent(inode, 16, 8, true, false);
	KUNIT_ASSERT_TRUE(test, ext4_is_pending(inode, 16));
	reset_da_stub_stats(ctx);

	es_remove_extent(inode, 16, 8);
	KUNIT_EXPECT_EQ(test, ctx->da_release_calls, 0);
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 16));
}

/*
 * Group 7: Shrinker, Referenced Aging, and Cache Clearing
 */
static void
test_es_clear_inode_es_retains_only_delayed_extents(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	static const struct expected_es only_delayed_exp[] = {
		{ .lblk = 40, .len = 5, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};

	es_insert_extent(inode, 10, 5, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 20, 5, 200, EXTENT_STATUS_UNWRITTEN, false);
	es_insert_extent(inode, 30, 5, 0, EXTENT_STATUS_HOLE, false);
	es_insert_delayed_extent(inode, 40, 5, false, false);
	ext4_set_inode_state(inode, EXT4_STATE_EXT_PRECACHED);

	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_all_nr, 4U);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 3U);

	ext4_clear_inode_es(inode);

	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode,
						 EXT4_STATE_EXT_PRECACHED));
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei->i_es_list));
	assert_es_tree(test, inode, only_delayed_exp,
		       ARRAY_SIZE(only_delayed_exp));
}

static void
test_es_shrinker_ages_referenced_and_keeps_delayed(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct percpu_counter *shk_cnt = &ctx->sbi->s_es_stats.es_stats_shk_cnt;
	struct shrink_control sc = { .nr_to_scan = 0 };
	struct extent_status es;
	unsigned long count, shrunk;
	static const struct expected_es after_first_scan[] = {
		{ .lblk = 10, .len = 2, .pblk = 100,
		  .type = EXTENT_STATUS_WRITTEN },
		{ .lblk = 30, .len = 2, .pblk = 300,
		  .type = EXTENT_STATUS_WRITTEN },
		{ .lblk = 50, .len = 2, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};
	static const struct expected_es after_second_scan[] = {
		{ .lblk = 50, .len = 2, .pblk = 0,
		  .type = EXTENT_STATUS_DELAYED },
	};

	/* Insert 4 reclaimable WRITTEN extents and 1 non-reclaimable DELAYED */
	es_insert_extent(inode, 10, 2, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 20, 2, 200, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 30, 2, 300, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 40, 2, 400, EXTENT_STATUS_WRITTEN, false);
	es_insert_delayed_extent(inode, 50, 2, false, false);

	/* Mark [10, 2) and [30, 2) as EXTENT_STATUS_REFERENCED via lookup */
	KUNIT_ASSERT_EQ(test,
			ext4_es_lookup_extent(inode, 10, NULL, &es, NULL), 1);
	KUNIT_ASSERT_EQ(test,
			ext4_es_lookup_extent(inode, 30, NULL, &es, NULL), 1);

	/*
	 * Fold per-CPU counter deltas into the central count so count_objects
	 * (which uses percpu_counter_read_positive) sees all 4 extents on SMP.
	 */
	percpu_counter_set(shk_cnt, percpu_counter_sum_positive(shk_cnt));
	count = es_shrinker_count(ctx, &sc);
	KUNIT_EXPECT_EQ(test, count, 4UL);

	/*
	 * First scan (nr_to_scan = 5):
	 * - [20, 2) and [40, 2) are unreferenced and reclaimed (shrunk = 2)
	 * - [10, 2) and [30, 2) have REFERENCED cleared and survive
	 * - [50, 2) is DELAYED (must_keep) and survives
	 */
	sc.nr_to_scan = 5;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 2UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 2U);
	assert_es_tree(test, inode, after_first_scan,
		       ARRAY_SIZE(after_first_scan));

	/*
	 * Second scan (nr_to_scan = 5):
	 * - [10, 2) and [30, 2) are now unreferenced and reclaimed (shrunk = 2)
	 * - Only [50, 2) DELAYED remains; inode is removed from s_es_list
	 */
	sc.nr_to_scan = 5;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 2UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei->i_es_list));
	assert_es_tree(test, inode, after_second_scan,
		       ARRAY_SIZE(after_second_scan));
}

static void
test_es_shrinker_cursor_advances_and_wraps_around(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct shrink_control sc = {};
	unsigned long shrunk;

	es_insert_extent(inode, 10, 2, 100, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 20, 2, 200, EXTENT_STATUS_WRITTEN, false);
	es_insert_extent(inode, 30, 2, 300, EXTENT_STATUS_WRITTEN, false);

	sc.nr_to_scan = 1;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 1UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shrink_lblk, 20U);

	/* Re-insert [10, 2) behind the shrink cursor; next scan wraps to it */
	es_insert_extent(inode, 10, 2, 100, EXTENT_STATUS_WRITTEN, false);
	sc.nr_to_scan = 3;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 3UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_all_nr, 0U);
}

static void
test_es_shrinker_skips_precached_inode_until_last_resort(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode1 = &ctx->ei->vfs_inode;
	struct inode *inode2;
	struct shrink_control sc = {};
	unsigned long shrunk;

	inode2 = es_alloc_test_inode(test, ctx->sb, 13);

	es_insert_extent(inode1, 10, 2, 100, EXTENT_STATUS_WRITTEN, false);
	ext4_set_inode_state(inode1, EXT4_STATE_EXT_PRECACHED);
	es_insert_extent(inode2, 10, 2, 200, EXTENT_STATUS_WRITTEN, false);

	/* First scan skips precached inode1 and reclaims from inode2 */
	sc.nr_to_scan = 1;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 1UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 1U);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode2)->i_es_shk_nr, 0U);

	/*
	 * Now only precached inode1 remains on s_es_list; shrinker retries
	 * and reclaims from inode1 as a last resort.
	 */
	sc.nr_to_scan = 1;
	shrunk = es_shrinker_scan(ctx, &sc);
	KUNIT_EXPECT_EQ(test, shrunk, 1UL);
	KUNIT_EXPECT_EQ(test, ctx->ei->i_es_shk_nr, 0U);
}

static void test_es_seq_shrinker_info_show_formats_stats(struct kunit *test)
{
	struct es_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei->vfs_inode;
	struct seq_file seq = {};
	char *buf;

	es_insert_extent(inode, 10, 2, 100, EXTENT_STATUS_WRITTEN, false);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	seq.buf = buf;
	seq.size = PAGE_SIZE;
	seq.private = ctx->sb;

	KUNIT_EXPECT_EQ(test,
			ext4_seq_es_shrinker_info_show(&seq, SEQ_START_TOKEN),
			0);
	KUNIT_EXPECT_GT(test, seq.count, 0UL);
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "reclaimable objects"));
}

static struct kunit_case extents_status_test_cases[] = {
	KUNIT_CASE(test_es_bitfield_written_and_unwritten_encoding),
	KUNIT_CASE(test_es_bitfield_delayed_and_hole_encoding),
	KUNIT_CASE(test_es_bitfield_referenced_and_pblock_update_preserve_state),
	KUNIT_CASE(test_es_lookup_empty_tree_returns_miss),
	KUNIT_CASE(test_es_insert_zero_len_is_noop),
	KUNIT_CASE(test_es_lookup_hits_cache_and_rbtree_with_next_lblk),
	KUNIT_CASE(test_es_fc_replay_bypasses_tree_operations),
	KUNIT_CASE_PARAM(test_es_merge_cases, es_merge_gen_params),
	KUNIT_CASE(test_es_insert_adjacent_delayed_extents_merges),
	KUNIT_CASE(test_es_merge_preserves_referenced),
	KUNIT_CASE(test_es_merge_at_max_blocks_boundary_succeeds),
	KUNIT_CASE_PARAM(test_es_remove_geometries, es_remove_gen_params),
	KUNIT_CASE(test_es_remove_multi_extent_span_trims_and_deletes),
	KUNIT_CASE(test_es_insert_overwrite_split_and_remerge),
	KUNIT_CASE(test_es_cache_extent_inserts_and_coalesces_same_status),
	KUNIT_CASE(test_es_cache_extent_conflict_covered_range_warns),
	KUNIT_CASE(test_es_cache_extent_conflict_partial_overlap_warns),
	KUNIT_CASE(test_es_cache_extent_hole_over_delayed_does_not_warn),
	KUNIT_CASE(test_es_find_extent_range_skips_nonmatching_and_finds_next),
	KUNIT_CASE(test_es_scan_range_matches_only_overlapping_extents),
	KUNIT_CASE(test_es_scan_clu_matches_extent_in_same_cluster),
	KUNIT_CASE(test_es_delalloc_remove_and_overwrite_account_reserved_blocks),
	KUNIT_CASE(test_es_bigalloc_insert_delayed_creates_and_removes_pending),
	KUNIT_CASE(test_es_bigalloc_revise_pending_single_cluster_halves),
	KUNIT_CASE(test_es_bigalloc_revise_pending_multi_cluster_span),
	KUNIT_CASE(test_es_bigalloc_remove_partial_cluster_holds_rsvd_until_empty),
	KUNIT_CASE(test_es_bigalloc_remove_fragmented_cluster_releases_once),
	KUNIT_CASE(test_es_bigalloc_remove_pending_cluster_cancels_pending_only),
	KUNIT_CASE(test_es_clear_inode_es_retains_only_delayed_extents),
	KUNIT_CASE(test_es_shrinker_ages_referenced_and_keeps_delayed),
	KUNIT_CASE(test_es_shrinker_cursor_advances_and_wraps_around),
	KUNIT_CASE(test_es_shrinker_skips_precached_inode_until_last_resort),
	KUNIT_CASE(test_es_seq_shrinker_info_show_formats_stats),
	{}
};

static struct kunit_suite extents_status_test_suite = {
	.name = "ext4_extents_status_test",
	.init = es_kunit_init,
	.test_cases = extents_status_test_cases,
};

kunit_test_suites(&extents_status_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 extents status tree and pending reservations");
MODULE_LICENSE("GPL");
