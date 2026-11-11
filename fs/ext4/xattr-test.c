// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 extended attributes (fs/ext4/xattr.c).
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/iversion.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/xattr.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"

#define TEST_BLK_SIZE		1024
#define TEST_BLK_BITS		10
#define TEST_INODE_SIZE		256
#define TEST_EXTRA_ISIZE	(sizeof(struct ext4_inode) - \
				 EXT4_GOOD_OLD_INODE_SIZE)
#define TEST_FIRST_PBLK		100
#define TEST_MAX_BLOCKS		16
#define TEST_MAX_EA_INODES	8
#define TEST_PRIMARY_INO	12
#define TEST_SECONDARY_INO	13
#define TEST_FIRST_EA_INO	20
#define TEST_CSUM_SEED		0x87654321U

struct mock_block {
	bool in_use;
	struct buffer_head bh;
	char data[TEST_BLK_SIZE] __aligned(8);
};

struct mock_inode_slot {
	bool allocated;
	struct ext4_inode_info ei;
	struct buffer_head iloc_bh;
	char raw_buf[TEST_INODE_SIZE] __aligned(8);
	ext4_fsblk_t lblk_to_pblk[8];
};

struct xattr_test_ctx {
	struct super_block sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct block_device bdev;
	struct address_space bd_mapping;
	struct buffer_head sb_bh;
	struct dentry root_dentry;

	struct mock_inode_slot primary;
	struct mock_inode_slot secondary;
	struct mock_inode_slot ea_inodes[TEST_MAX_EA_INODES];
	struct mock_block blocks[TEST_MAX_BLOCKS];

	int fail_get_inode_loc;
	int fail_mark_iloc_dirty;
	int fail_dirty_metadata;
	int fail_alloc_block;
	int fail_sb_bread;
	int fail_getblk;
	int fail_iget;
	int fail_new_inode;
	bool fail_xattr_getblk;

	unsigned int alloc_block_calls;
	unsigned int free_block_calls;
	ext4_fsblk_t last_freed_block;
	unsigned int mark_iloc_dirty_calls;
	unsigned int mark_inode_dirty_calls;
	unsigned int dirty_metadata_calls;
};

#ifdef CONFIG_QUOTA
static struct dquot __rcu **xattr_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *xattr_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations xattr_test_qops = {
	.get_reserved_space = xattr_test_get_reserved_space,
};
#endif

static const struct super_operations xattr_test_sops = {
#ifdef CONFIG_QUOTA
	.get_dquots	= xattr_test_get_dquots,
#endif
};

static void init_mock_inode_slot(struct xattr_test_ctx *ctx,
				 struct mock_inode_slot *slot,
				 unsigned long ino)
{
	struct inode *inode = &slot->ei.vfs_inode;
	struct ext4_inode *raw_inode;

	memset(slot, 0, sizeof(*slot));
	slot->allocated = true;

	inode->i_sb = &ctx->sb;
	inode->i_ino = ino;
	inode->i_mode = S_IFREG | 0644;
	inode->i_blkbits = TEST_BLK_BITS;
	inode->i_mapping = &inode->i_data;
	set_nlink(inode, 1);
	atomic_set(&inode->i_count, 1);

	spin_lock_init(&inode->i_lock);
	i_size_ordered_init(inode);
	init_rwsem(&inode->i_rwsem);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	INIT_HLIST_NODE(&inode->i_hash);
	/*
	 * Pre-set all I_DIRTY flags so mark_inode_dirty() from quota helpers
	 * sees the flags already set and returns without touching global bdi
	 * writeback lists.
	 */
	inode_state_set_raw(inode, I_DIRTY);

	init_rwsem(&slot->ei.xattr_sem);
	init_rwsem(&slot->ei.i_data_sem);
	spin_lock_init(&slot->ei.i_raw_lock);
	spin_lock_init(&slot->ei.i_block_reservation_lock);
	INIT_LIST_HEAD(&slot->ei.i_orphan);
	slot->ei.i_extra_isize = TEST_EXTRA_ISIZE;
	slot->ei.i_csum_seed = TEST_CSUM_SEED ^ (u32)ino;

	slot->iloc_bh.b_data = slot->raw_buf;
	slot->iloc_bh.b_size = TEST_INODE_SIZE;
	slot->iloc_bh.b_blocknr = 10 + ino;
	slot->iloc_bh.b_bdev = &ctx->bdev;
	atomic_set(&slot->iloc_bh.b_count, 1);
	set_buffer_uptodate(&slot->iloc_bh);
	set_buffer_mapped(&slot->iloc_bh);

	raw_inode = (struct ext4_inode *)slot->raw_buf;
	raw_inode->i_extra_isize = cpu_to_le16(slot->ei.i_extra_isize);
}

static struct mock_block *alloc_mock_block(struct xattr_test_ctx *ctx)
{
	int i;

	for (i = 0; i < TEST_MAX_BLOCKS; i++) {
		struct mock_block *blk = &ctx->blocks[i];

		if (!blk->in_use) {
			memset(blk, 0, sizeof(*blk));
			blk->in_use = true;
			blk->bh.b_data = blk->data;
			blk->bh.b_size = TEST_BLK_SIZE;
			blk->bh.b_blocknr = TEST_FIRST_PBLK + i;
			blk->bh.b_bdev = &ctx->bdev;
			atomic_set(&blk->bh.b_count, 1);
			set_buffer_uptodate(&blk->bh);
			set_buffer_mapped(&blk->bh);
			return blk;
		}
	}
	return NULL;
}

static struct mock_block *find_mock_block(struct xattr_test_ctx *ctx,
					  ext4_fsblk_t blocknr)
{
	int i;

	for (i = 0; i < TEST_MAX_BLOCKS; i++) {
		if (ctx->blocks[i].in_use &&
		    ctx->blocks[i].bh.b_blocknr == blocknr)
			return &ctx->blocks[i];
	}
	return NULL;
}

static int ext4_get_inode_loc_stub(struct inode *inode, struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;

	if (ctx->fail_get_inode_loc)
		return ctx->fail_get_inode_loc;

	slot = container_of(EXT4_I(inode), struct mock_inode_slot, ei);
	get_bh(&slot->iloc_bh);
	iloc->bh = &slot->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int ext4_mark_iloc_dirty_stub(handle_t *handle, struct inode *inode,
				     struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;

	brelse(iloc->bh);
	ctx->mark_iloc_dirty_calls++;
	if (ctx->fail_mark_iloc_dirty)
		return ctx->fail_mark_iloc_dirty;
	return 0;
}

static int __ext4_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
					const char *func, unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;

	ctx->mark_inode_dirty_calls++;
	return 0;
}

static int __ext4_handle_dirty_metadata_stub(const char *where,
					     unsigned int line,
					     handle_t *handle,
					     struct inode *inode,
					     struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;

	if (ctx->fail_dirty_metadata)
		return ctx->fail_dirty_metadata;
	if (bh)
		set_buffer_uptodate(bh);
	ctx->dirty_metadata_calls++;
	return 0;
}

static ext4_fsblk_t ext4_mb_new_blocks_stub(handle_t *handle,
					    struct ext4_allocation_request *ar,
					    int *errp)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_block *blk;

	if (ctx->fail_alloc_block) {
		*errp = ctx->fail_alloc_block;
		return 0;
	}

	blk = alloc_mock_block(ctx);
	if (!blk) {
		*errp = -ENOSPC;
		return 0;
	}

	ctx->alloc_block_calls++;
	*errp = 0;
	return blk->bh.b_blocknr;
}

static void ext4_free_blocks_stub(handle_t *handle, struct inode *inode,
				  struct buffer_head *bh, ext4_fsblk_t block,
				  unsigned long count, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_block *blk;

	if (bh) {
		if (!block)
			block = bh->b_blocknr;
		if (flags & EXT4_FREE_BLOCKS_FORGET)
			brelse(bh);
	}
	if (!(flags & EXT4_FREE_BLOCKS_NO_QUOT_UPDATE))
		dquot_free_block(inode,
				 EXT4_C2B(EXT4_SB(inode->i_sb), count));

	blk = find_mock_block(ctx, block);
	if (blk)
		blk->in_use = false;

	ctx->free_block_calls++;
	ctx->last_freed_block = block;
}

static struct buffer_head *ext4_xattr_getblk_stub(struct super_block *sb,
						  ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_block *blk;

	if (ctx->fail_xattr_getblk)
		return NULL;

	blk = find_mock_block(ctx, block);
	if (!blk)
		return NULL;

	get_bh(&blk->bh);
	return &blk->bh;
}

static struct buffer_head *ext4_sb_bread_stub(struct super_block *sb,
					      sector_t block,
					      blk_opf_t op_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_block *blk;

	if (ctx->fail_sb_bread)
		return ERR_PTR(ctx->fail_sb_bread);

	blk = find_mock_block(ctx, block);
	if (!blk)
		return ERR_PTR(-EIO);

	get_bh(&blk->bh);
	return &blk->bh;
}

static int ext4_map_blocks_stub(handle_t *handle, struct inode *inode,
				struct ext4_map_blocks *map, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;
	struct mock_block *blk;

	slot = container_of(EXT4_I(inode), struct mock_inode_slot, ei);
	if (map->m_lblk >= ARRAY_SIZE(slot->lblk_to_pblk))
		return -EFSCORRUPTED;

	if (!slot->lblk_to_pblk[map->m_lblk]) {
		if (!(flags & EXT4_GET_BLOCKS_CREATE))
			return 0;
		if (ctx->fail_alloc_block)
			return ctx->fail_alloc_block;
		blk = alloc_mock_block(ctx);
		if (!blk)
			return -ENOSPC;
		slot->lblk_to_pblk[map->m_lblk] = blk->bh.b_blocknr;
		inode->i_blocks += TEST_BLK_SIZE >> 9;
		map->m_flags = EXT4_MAP_NEW | EXT4_MAP_MAPPED;
	} else {
		map->m_flags = EXT4_MAP_MAPPED;
	}

	map->m_pblk = slot->lblk_to_pblk[map->m_lblk];
	map->m_len = 1;
	return 1;
}

static struct buffer_head *ext4_getblk_stub(handle_t *handle,
					    struct inode *inode,
					    ext4_lblk_t block, int map_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;
	struct mock_block *blk;

	if (ctx->fail_getblk)
		return ERR_PTR(ctx->fail_getblk);

	slot = container_of(EXT4_I(inode), struct mock_inode_slot, ei);
	if (block >= ARRAY_SIZE(slot->lblk_to_pblk))
		return ERR_PTR(-EFSCORRUPTED);
	if (!slot->lblk_to_pblk[block]) {
		if (!(map_flags & EXT4_GET_BLOCKS_CREATE))
			return NULL;
		blk = alloc_mock_block(ctx);
		if (!blk)
			return ERR_PTR(-ENOSPC);
		slot->lblk_to_pblk[block] = blk->bh.b_blocknr;
		inode->i_blocks += TEST_BLK_SIZE >> 9;
	} else {
		blk = find_mock_block(ctx, slot->lblk_to_pblk[block]);
		if (!blk)
			return ERR_PTR(-EIO);
	}

	get_bh(&blk->bh);
	return &blk->bh;
}

static struct inode *__ext4_new_inode_stub(const struct mnt_idmap *idmap,
					   handle_t *handle,
					   struct inode *dir, umode_t mode,
					   const struct qstr *qstr,
					   __u32 goal, uid_t *owner,
					   __u32 i_flags, int handle_type,
					   unsigned int line_no, int nblocks)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	int i;

	if (ctx->fail_new_inode)
		return ERR_PTR(ctx->fail_new_inode);

	for (i = 0; i < TEST_MAX_EA_INODES; i++) {
		struct mock_inode_slot *slot = &ctx->ea_inodes[i];
		struct inode *inode = &slot->ei.vfs_inode;

		if (!slot->allocated) {
			init_mock_inode_slot(ctx, slot, TEST_FIRST_EA_INO + i);
			inode->i_mode = mode;
			slot->ei.i_flags = i_flags;
			if (owner) {
				i_uid_write(inode, owner[0]);
				i_gid_write(inode, owner[1]);
			}
			/*
			 * Set I_NEW for unlock_new_inode() and give the mock
			 * inode an extra baseline reference (i_count = 2) so
			 * ext4_put_ea_inode()'s iput_if_not_last() decrements
			 * i_count to 1 and returns without entering VFS
			 * eviction.
			 */
			inode_state_set_raw(inode, I_DIRTY | I_NEW);
			atomic_set(&inode->i_count, 2);
			return inode;
		}
	}
	return ERR_PTR(-ENOSPC);
}

static struct inode *__ext4_iget_stub(struct super_block *sb,
				      unsigned long ino,
				      ext4_iget_flags flags,
				      const char *function,
				      unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct xattr_test_ctx *ctx = test->priv;
	int i;

	if (ctx->fail_iget)
		return ERR_PTR(ctx->fail_iget);

	for (i = 0; i < TEST_MAX_EA_INODES; i++) {
		struct mock_inode_slot *slot = &ctx->ea_inodes[i];
		struct inode *inode = &slot->ei.vfs_inode;

		if (slot->allocated && inode->i_ino == ino) {
			if ((flags & EXT4_IGET_EA_INODE) &&
			    !(slot->ei.i_flags & EXT4_EA_INODE_FL))
				return ERR_PTR(-EFSCORRUPTED);
			atomic_inc(&inode->i_count);
			return inode;
		}
	}
	return ERR_PTR(-ENOENT);
}

static int xattr_test_init(struct kunit *test)
{
	struct xattr_test_ctx *ctx;
	struct super_block *sb;
	struct ext4_sb_info *sbi;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	sb = &ctx->sb;
	sbi = &ctx->sbi;

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &xattr_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &xattr_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_blocksize = TEST_BLK_SIZE;
	sb->s_blocksize_bits = TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_xattr_kunit", sizeof(sb->s_id));
	init_rwsem(&sb->s_umount);

	ctx->bdev.bd_mapping = &ctx->bd_mapping;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	sbi->s_es = &ctx->es;
	ctx->es.s_first_data_block = cpu_to_le32(1);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_inodes_count = cpu_to_le32(1024);
	ctx->es.s_first_ino = cpu_to_le32(11);
	ctx->es.s_inode_size = cpu_to_le16(TEST_INODE_SIZE);
	ext4_blocks_count_set(&ctx->es, 65536);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	sbi->s_inode_size = TEST_INODE_SIZE;
	sbi->s_first_ino = 11;
	sbi->s_want_extra_isize = TEST_EXTRA_ISIZE;
	ctx->es.s_min_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);
	sbi->s_blocks_per_group = 8192;
	sbi->s_clusters_per_group = 8192;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = 8;
	sbi->s_csum_seed = TEST_CSUM_SEED;

	set_opt(sb, ERRORS_CONT);
	set_opt(sb, XATTR_USER);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);

	/*
	 * Use burst = 0 so expected corruption-test error/warning messages
	 * are suppressed without setting EXT4_FLAGS_SHUTDOWN.
	 */
	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	init_mock_inode_slot(ctx, &ctx->primary, TEST_PRIMARY_INO);
	init_mock_inode_slot(ctx, &ctx->secondary, TEST_SECONDARY_INO);

	ctx->root_dentry.d_sb = sb;
	ctx->root_dentry.d_inode = &ctx->primary.ei.vfs_inode;
	sb->s_root = &ctx->root_dentry;

	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   ext4_get_inode_loc_stub);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   ext4_mark_iloc_dirty_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   __ext4_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   __ext4_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   ext4_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   ext4_free_blocks_stub);
	kunit_activate_static_stub(test, ext4_xattr_getblk,
				   ext4_xattr_getblk_stub);
	kunit_activate_static_stub(test, ext4_sb_bread,
				   ext4_sb_bread_stub);
	kunit_activate_static_stub(test, ext4_map_blocks,
				   ext4_map_blocks_stub);
	kunit_activate_static_stub(test, ext4_getblk,
				   ext4_getblk_stub);
	kunit_activate_static_stub(test, __ext4_new_inode,
				   __ext4_new_inode_stub);
	kunit_activate_static_stub(test, __ext4_iget,
				   __ext4_iget_stub);

	return 0;
}

static void xattr_test_exit(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;

	if (!ctx)
		return;

	ext4_xattr_destroy_cache(ctx->sbi.s_ea_block_cache);
	ctx->sbi.s_ea_block_cache = NULL;
	ext4_xattr_destroy_cache(ctx->sbi.s_ea_inode_cache);
	ctx->sbi.s_ea_inode_cache = NULL;
}

/*
 * Test 1: Sizing macros, EXT4_INODE_HAS_XATTR_SPACE, and xattr_sem locking
 * helpers.
 */
static void test_xattr_macros_and_locking(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	int save_outer = -1, save_inner = -1;
	int locked;

	/* EXT4_XATTR_LEN rounds (sizeof(entry) + name_len) to 4-byte boundary */
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_LEN(0), 16U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_LEN(1), 20U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_LEN(4), 20U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_LEN(5), 24U);

	/* EXT4_XATTR_SIZE rounds value size up to 4-byte boundary */
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_SIZE(0), 0U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_SIZE(1), 4U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_SIZE(4), 4U);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_SIZE(5), 8U);

	/*
	 * EXT4_XATTR_MIN_LARGE_EA_SIZE(b) is
	 * b - EXT4_XATTR_LEN(3) - sizeof(struct ext4_xattr_header) - 4.
	 */
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_MIN_LARGE_EA_SIZE(1024), 968);
	KUNIT_EXPECT_EQ(test, EXT4_XATTR_MIN_LARGE_EA_SIZE(4096), 4040);

	/*
	 * EXT4_INODE_HAS_XATTR_SPACE requires at least
	 * sizeof(ibody_header) + EXT4_XATTR_PAD = 8 bytes of space after
	 * EXT4_GOOD_OLD_INODE_SIZE + i_extra_isize.
	 */
	KUNIT_EXPECT_TRUE(test, EXT4_INODE_HAS_XATTR_SPACE(inode));

	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	KUNIT_EXPECT_FALSE(test, EXT4_INODE_HAS_XATTR_SPACE(inode));
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;

	/* Exact boundary: 256 - 128 - 8 = 120 bytes of i_extra_isize fits */
	EXT4_I(inode)->i_extra_isize = 120;
	KUNIT_EXPECT_TRUE(test, EXT4_INODE_HAS_XATTR_SPACE(inode));

	/* 124 bytes leaves only 4 bytes (< 8 bytes required) */
	EXT4_I(inode)->i_extra_isize = 124;
	KUNIT_EXPECT_FALSE(test, EXT4_INODE_HAS_XATTR_SPACE(inode));
	EXT4_I(inode)->i_extra_isize = TEST_EXTRA_ISIZE;

	/* Locking helpers and EXT4_STATE_NO_EXPAND state preservation */
	KUNIT_EXPECT_EQ(test,
			ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND), 0);

	ext4_write_lock_xattr(inode, &save_outer);
	locked = ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND);
	ext4_write_unlock_xattr(inode, &save_outer);

	KUNIT_EXPECT_EQ(test, save_outer, 0);
	KUNIT_EXPECT_NE(test, locked, 0);
	KUNIT_EXPECT_EQ(test,
			ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND), 0);

	/* When EXT4_STATE_NO_EXPAND is already set, unlock must preserve it */
	ext4_set_inode_state(inode, EXT4_STATE_NO_EXPAND);
	locked = ext4_write_trylock_xattr(inode, &save_inner);
	if (locked)
		ext4_write_unlock_xattr(inode, &save_inner);

	KUNIT_EXPECT_EQ(test, locked, 1);
	KUNIT_EXPECT_NE(test, save_inner, 0);
	KUNIT_EXPECT_NE(test,
			ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND), 0);
	ext4_clear_inode_state(inode, EXT4_STATE_NO_EXPAND);
}

/*
 * Test 2: Parameterized xattr_check_inode() validation across valid and
 * corrupted in-inode xattr layouts.
 */
enum xattr_check_case_id {
	CHECK_VALID_EMPTY,
	CHECK_VALID_TWO_ENTRIES,
	CHECK_BAD_MAGIC,
	CHECK_END_BEFORE_FIRST,
	CHECK_ENTRY_HDR_OVERFLOW,
	CHECK_ENTRY_NAME_OVERFLOW,
	CHECK_UNTERMINATED_LIST,
	CHECK_EA_INODE_WITHOUT_FEATURE,
	CHECK_VALUE_SIZE_GT_TOTAL,
	CHECK_VALUE_OFFS_OVERLAPS_ENTRY,
	CHECK_VALUE_END_EXCEEDS_TOTAL,
};

struct xattr_check_param {
	const char *desc;
	enum xattr_check_case_id id;
	int expected_err;
};

static const struct xattr_check_param xattr_check_params[] = {
	{ "valid_empty_ibody", CHECK_VALID_EMPTY, 0 },
	{ "valid_two_entries", CHECK_VALID_TWO_ENTRIES, 0 },
	{ "bad_magic", CHECK_BAD_MAGIC, -EFSCORRUPTED },
	{ "end_before_first_entry", CHECK_END_BEFORE_FIRST, -EFSCORRUPTED },
	{ "entry_header_overflows_end", CHECK_ENTRY_HDR_OVERFLOW, -EFSCORRUPTED },
	{ "entry_name_overflows_end", CHECK_ENTRY_NAME_OVERFLOW, -EFSCORRUPTED },
	{ "unterminated_entry_list", CHECK_UNTERMINATED_LIST, -EFSCORRUPTED },
	{ "ea_inode_without_feature", CHECK_EA_INODE_WITHOUT_FEATURE, -EFSCORRUPTED },
	{ "value_size_gt_total_size", CHECK_VALUE_SIZE_GT_TOTAL, -EFSCORRUPTED },
	{ "value_offset_overlaps_entries", CHECK_VALUE_OFFS_OVERLAPS_ENTRY, -EFSCORRUPTED },
	{ "value_end_exceeds_total_size", CHECK_VALUE_END_EXCEEDS_TOTAL, -EFSCORRUPTED },
};

static void xattr_check_param_get_desc(const struct xattr_check_param *p,
				       char *desc)
{
	strscpy(desc, p->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(xattr_check, xattr_check_params, xattr_check_param_get_desc);

static void test_xattr_check_inode(struct kunit *test)
{
	const struct xattr_check_param *p = test->param_value;
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct ext4_inode *raw_inode = (struct ext4_inode *)ctx->primary.raw_buf;
	struct ext4_xattr_ibody_header *hdr = IHDR(inode, raw_inode);
	struct ext4_xattr_entry *e1, *e2;
	void *end = ITAIL(inode, raw_inode);
	size_t total_size = (char *)end - (char *)IFIRST(hdr);
	int ret;

	memset(hdr, 0, (char *)end - (char *)hdr);
	hdr->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
	e1 = IFIRST(hdr);

	switch (p->id) {
	case CHECK_VALID_EMPTY:
		break;
	case CHECK_VALID_TWO_ENTRIES:
		e1->e_name_len = 1;
		e1->e_name_index = EXT4_XATTR_INDEX_USER;
		e1->e_name[0] = 'a';
		e1->e_value_size = cpu_to_le32(4);
		e1->e_value_offs = cpu_to_le16(total_size - 4);

		e2 = EXT4_XATTR_NEXT(e1);
		e2->e_name_len = 1;
		e2->e_name_index = EXT4_XATTR_INDEX_USER;
		e2->e_name[0] = 'b';
		e2->e_value_size = cpu_to_le32(4);
		e2->e_value_offs = cpu_to_le16(total_size - 8);
		break;
	case CHECK_BAD_MAGIC:
		hdr->h_magic = cpu_to_le32(0xdeadbeef);
		break;
	case CHECK_END_BEFORE_FIRST:
		end = (char *)IFIRST(hdr) + 2;
		break;
	case CHECK_ENTRY_HDR_OVERFLOW:
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		end = (char *)IFIRST(hdr) + EXT4_XATTR_LEN(1) + 8;
		e2 = EXT4_XATTR_NEXT(e1);
		e2->e_name_len = 1;
		break;
	case CHECK_ENTRY_NAME_OVERFLOW:
		e1->e_name_len = 100;
		break;
	case CHECK_UNTERMINATED_LIST:
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		end = (char *)IFIRST(hdr) + EXT4_XATTR_LEN(1);
		break;
	case CHECK_EA_INODE_WITHOUT_FEATURE:
		ext4_clear_feature_ea_inode(&ctx->sb);
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		e1->e_value_inum = cpu_to_le32(20);
		e1->e_value_size = cpu_to_le32(4);
		break;
	case CHECK_VALUE_SIZE_GT_TOTAL:
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		e1->e_value_size = cpu_to_le32(total_size + 4);
		e1->e_value_offs = cpu_to_le16(total_size - 4);
		break;
	case CHECK_VALUE_OFFS_OVERLAPS_ENTRY:
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		e1->e_value_size = cpu_to_le32(4);
		/* Offset points inside e1 + 4-byte terminator */
		e1->e_value_offs = cpu_to_le16(EXT4_XATTR_LEN(1));
		break;
	case CHECK_VALUE_END_EXCEEDS_TOTAL:
		e1->e_name_len = 1;
		e1->e_name[0] = 'a';
		e1->e_value_size = cpu_to_le32(8);
		e1->e_value_offs = cpu_to_le16(total_size - 4);
		break;
	}

	ret = xattr_check_inode(inode, hdr, end);
	KUNIT_EXPECT_EQ_MSG(test, ret, p->expected_err, "case: %s", p->desc);
}

/*
 * Test 3: In-inode (ibody) get/set, flags validation, EXT4_STATE_NEW handling,
 * superblock feature update, and identical-value fastpath.
 */
static void test_xattr_ibody_basic_and_flags(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct ext4_inode *raw_inode = (struct ext4_inode *)ctx->primary.raw_buf;
	char long_name[260];
	char buf[32];
	unsigned int prev_dirty;
	int ret;

	/* Fresh inode without EXT4_STATE_XATTR returns -ENODATA */
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "missing",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, -ENODATA);

	/* Parameter validation: NULL name -> -EINVAL, >255 name -> -ERANGE */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    NULL, "v", 1, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    long_name, "v", 1, 0);
	KUNIT_EXPECT_EQ(test, ret, -ERANGE);

	/* Forced shutdown check on get and set */
	set_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "k",
				       buf, sizeof(buf)),
			-EIO);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_set_handle(NULL, inode,
					      EXT4_XATTR_INDEX_USER, "k",
					      "v", 1, 0),
			-EIO);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);

	/* XATTR_REPLACE on non-existent attribute returns -ENODATA */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "no_such", "val", 3, XATTR_REPLACE);
	KUNIT_EXPECT_EQ(test, ret, -ENODATA);

	/* Deleting non-existent attribute without XATTR_REPLACE succeeds (0) */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "no_such", NULL, 0, 0);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/*
	 * Verify EXT4_STATE_NEW causes ext4_xattr_set_handle() to zero the raw
	 * inode and clear EXT4_STATE_NEW.
	 */
	memset(raw_inode, 0x5a, TEST_INODE_SIZE);
	ext4_set_inode_state(inode, EXT4_STATE_NEW);
	KUNIT_EXPECT_FALSE(test,
			   ext4_has_feature_xattr(&ctx->sb));

	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "key1", "hello", 5, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
			ext4_test_inode_state(inode, EXT4_STATE_NEW), 0);
	KUNIT_EXPECT_NE(test,
			ext4_test_inode_state(inode, EXT4_STATE_XATTR), 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(IHDR(inode, raw_inode)->h_magic),
			(u32)EXT4_XATTR_MAGIC);
	/* Setting first xattr enables EXT4_FEATURE_COMPAT_EXT_ATTR on sb */
	KUNIT_EXPECT_TRUE(test, ext4_has_feature_xattr(&ctx->sb));

	/* XATTR_CREATE on existing attribute returns -EEXIST */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "key1", "other", 5, XATTR_CREATE);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	/* Query length (NULL buffer), too small buffer (-ERANGE), and read */
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "key1", NULL, 0);
	KUNIT_EXPECT_EQ(test, ret, 5);

	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "key1", buf, 4);
	KUNIT_EXPECT_EQ(test, ret, -ERANGE);

	memset(buf, 0, sizeof(buf));
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "key1",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_MEMEQ(test, buf, "hello", 5);

	/* Direct ext4_xattr_ibody_get() check */
	memset(buf, 0, sizeof(buf));
	ret = ext4_xattr_ibody_get(inode, EXT4_XATTR_INDEX_USER, "key1",
				   buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_MEMEQ(test, buf, "hello", 5);

	/*
	 * Setting the exact same value triggers the ext4_xattr_value_same()
	 * fastpath and skips dirtying the inode location.
	 */
	prev_dirty = ctx->mark_iloc_dirty_calls;
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "key1", "hello", 5, XATTR_REPLACE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->mark_iloc_dirty_calls, prev_dirty);
}

/*
 * Test 4: In-inode (ibody) replacement (same padded size vs different size),
 * zero-length values, entry removal, and i_inline_off adjustment.
 */
static void test_xattr_ibody_replace_and_remove(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct ext4_inode *raw_inode = (struct ext4_inode *)ctx->primary.raw_buf;
	struct ext4_xattr_ibody_header *hdr = IHDR(inode, raw_inode);
	struct ext4_xattr_entry *e;
	char buf[32];
	u16 offs_before, offs_after;
	int ret;

	/* Create two entries in ibody: "a" (7 bytes -> 8 padded) and "b" (4 bytes) */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "a", "1234567", 7, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "b", "wxyz", 4, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	e = IFIRST(hdr);
	offs_before = le16_to_cpu(e->e_value_offs);

	/*
	 * Replace "a" with a 5-byte value (still 8 bytes padded): hits the
	 * in-place overwrite fastpath without shifting offsets, and zeroes
	 * the last 3 padding bytes.
	 */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "a", "ABCDE", 5, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	offs_after = le16_to_cpu(e->e_value_offs);
	KUNIT_EXPECT_EQ(test, offs_after, offs_before);
	KUNIT_EXPECT_MEMEQ(test, (char *)IFIRST(hdr) + offs_after,
			   "ABCDE\0\0\0", 8);

	/*
	 * Replace "a" with a 12-byte value (different padded size): shifts
	 * "b"'s value and updates offsets while preserving "b"'s content.
	 */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "a", "0123456789ab", 12, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "a",
				       buf, sizeof(buf)),
			12);
	KUNIT_EXPECT_MEMEQ(test, buf, "0123456789ab", 12);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "b",
				       buf, sizeof(buf)),
			4);
	KUNIT_EXPECT_MEMEQ(test, buf, "wxyz", 4);

	/* Replace "a" with a zero-length value ("", size = 0) */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "a", "", 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "a",
				       buf, sizeof(buf)),
			0);

	/*
	 * Add system.data (inline data entry) and verify removing an earlier
	 * entry ("a") shifts system.data's entry header left and updates
	 * EXT4_I(inode)->i_inline_off.
	 */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_SYSTEM,
				    "data", "inlinedata", 10,
				    XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	offs_before = EXT4_I(inode)->i_inline_off;
	KUNIT_EXPECT_GT(test, offs_before, (u16)0);

	/* Remove "a": system.data entry header moves left by EXT4_XATTR_LEN(1) */
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "a", NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_inline_off,
			(u16)(offs_before - EXT4_XATTR_LEN(1)));

	/* Remove "b" and "system.data": emptying ibody clears h_magic and state */
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_USER,
				    "b", NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test,
			ext4_test_inode_state(inode, EXT4_STATE_XATTR), 0);

	ret = ext4_xattr_set_handle(NULL, inode, EXT4_XATTR_INDEX_SYSTEM,
				    "data", NULL, 0,
				    XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(hdr->h_magic), 0U);
	KUNIT_EXPECT_EQ(test,
			ext4_test_inode_state(inode, EXT4_STATE_XATTR), 0);
}

/*
 * Test 5: External xattr block allocation, entry sorting, migration between
 * ibody and external block, EXT4_XATTR_BLOCK_RESERVE enforcement, and external
 * block header/CRC32C checksum validation.
 */
static void test_xattr_external_block_and_check(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct mock_block *blk;
	struct ext4_xattr_header *bhdr;
	struct ext4_xattr_entry *e1, *e2, *e3;
	char val100[100];
	char *val796;
	char buf[128];
	int ret;

	ext4_set_feature_metadata_csum(&ctx->sb);
	memset(val100, 'V', sizeof(val100));

	/*
	 * Start with a small attribute in ibody ("migr", 4 bytes), then grow
	 * it to 100 bytes (> 88-byte ibody space) so it migrates from ibody
	 * to a newly allocated external block.
	 */
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "migr",
			     "tiny", 4, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "migr",
			     val100, sizeof(val100), XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test, EXT4_I(inode)->i_file_acl, 0ULL);
	KUNIT_EXPECT_EQ(test,
			ext4_test_inode_state(inode, EXT4_STATE_XATTR), 0);

	/*
	 * Add two more entries into the external block (by temporarily setting
	 * s_inode_size = 128 so ibody is bypassed) and verify external block
	 * entries are kept sorted by (e_name_index, e_name_len, e_name).
	 */
	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "z",
			     "1", 1, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "a",
			     "2", 1, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;

	blk = find_mock_block(ctx, EXT4_I(inode)->i_file_acl);
	KUNIT_ASSERT_NOT_NULL(test, blk);
	bhdr = BHDR(&blk->bh);
	e1 = BFIRST(&blk->bh);
	e2 = EXT4_XATTR_NEXT(e1);
	e3 = EXT4_XATTR_NEXT(e2);

	/* Length 1 ("a", "z") precede length 4 ("migr") */
	KUNIT_EXPECT_EQ(test, e1->e_name_len, 1);
	KUNIT_EXPECT_EQ(test, e1->e_name[0], 'a');
	KUNIT_EXPECT_EQ(test, e2->e_name_len, 1);
	KUNIT_EXPECT_EQ(test, e2->e_name[0], 'z');
	KUNIT_EXPECT_EQ(test, e3->e_name_len, 4);
	KUNIT_EXPECT_MEMEQ(test, e3->e_name, "migr", 4);

	/*
	 * Verify external block validation and buffer_verified caching:
	 * 1) Reading sets buffer_verified.
	 * 2) Corrupting h_checksum while buffer_verified is set still succeeds
	 *    because ext4_xattr_check_block() trusts buffer_verified.
	 * 3) Clearing buffer_verified exposes the bad checksum (-EFSBADCRC).
	 * 4) Corrupting h_blocks or h_magic returns -EFSCORRUPTED.
	 */
	clear_buffer_verified(&blk->bh);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "migr",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, 100);
	KUNIT_EXPECT_NE(test, buffer_verified(&blk->bh), 0);

	bhdr->h_checksum ^= cpu_to_le32(1);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "a",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, 1);

	clear_buffer_verified(&blk->bh);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "a",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, -EFSBADCRC);
	bhdr->h_checksum ^= cpu_to_le32(1);

	bhdr->h_blocks = cpu_to_le32(2);
	clear_buffer_verified(&blk->bh);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "a",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	bhdr->h_blocks = cpu_to_le32(1);

	/*
	 * Verify EXT4_XATTR_BLOCK_RESERVE (128 bytes on 1024B block) when
	 * EXT4_FEATURE_INCOMPAT_EA_INODE is enabled:
	 * With "migr" (100B), "a" (4B), and "z" (4B) in the block, min_offs is
	 * 1024 - 108 = 916 and free space is 820 bytes. Adding "r1" with a
	 * 796-byte value (816B total <= 820B free) leaves min_offs - new_size
	 * = 916 - 796 = 120 < 128 (EXT4_XATTR_BLOCK_RESERVE), so with ea_inode
	 * enabled and EA inode creation disabled it fails with -ENOSPC,
	 * whereas with ea_inode disabled it fits inline in the block.
	 */
	val796 = kunit_kmalloc(test, 796, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, val796);
	memset(val796, 'R', 796);
	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	ext4_set_feature_ea_inode(&ctx->sb);
	ctx->fail_new_inode = -ENOSPC;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "r1",
			     val796, 796, XATTR_CREATE);
	ctx->fail_new_inode = 0;
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);

	ext4_clear_feature_ea_inode(&ctx->sb);
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "r1",
			     val796, 796, XATTR_CREATE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test,
			ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "r1",
				       NULL, 0, XATTR_REPLACE),
			0);
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;

	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "a",
			      NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "z",
			     NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * Removing the last entry ("migr") from the external block frees the
	 * external xattr block and clears i_file_acl to 0.
	 */
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "migr",
			     NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);
}

/*
 * Test 6: External xattr block mb_cache deduplication, copy-on-write cloning
 * when modifying a shared block, and EXT4_XATTR_REFCOUNT_MAX handling.
 */
static void test_xattr_block_dedup_and_cow(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode1 = &ctx->primary.ei.vfs_inode;
	struct inode *inode2 = &ctx->secondary.ei.vfs_inode;
	struct mock_block *blk;
	struct mb_cache_entry *ce;
	char val100[100];
	ext4_fsblk_t shared_blk;
	u32 hash;
	int ret;

	ctx->sbi.s_ea_block_cache = ext4_xattr_create_cache();
	KUNIT_ASSERT_NOT_NULL(test, ctx->sbi.s_ea_block_cache);

	memset(val100, 'D', sizeof(val100));
	ret = ext4_xattr_set(inode1, EXT4_XATTR_INDEX_USER, "shared",
			     val100, sizeof(val100), XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	shared_blk = EXT4_I(inode1)->i_file_acl;
	KUNIT_ASSERT_NE(test, shared_blk, 0ULL);

	/* Setting identical xattr on inode2 reuses shared_blk from mb_cache */
	ret = ext4_xattr_set(inode2, EXT4_XATTR_INDEX_USER, "shared",
			     val100, sizeof(val100), XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode2)->i_file_acl, shared_blk);

	blk = find_mock_block(ctx, shared_blk);
	KUNIT_ASSERT_NOT_NULL(test, blk);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(BHDR(&blk->bh)->h_refcount), 2U);

	/*
	 * Modifying the xattr on inode2 triggers copy-on-write: inode2 gets a
	 * new block and shared_blk's refcount drops back to 1.
	 */
	val100[0] = 'M';
	ret = ext4_xattr_set(inode2, EXT4_XATTR_INDEX_USER, "shared",
			     val100, sizeof(val100), XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test, EXT4_I(inode2)->i_file_acl, shared_blk);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(BHDR(&blk->bh)->h_refcount), 1U);

	/*
	 * Test EXT4_XATTR_REFCOUNT_MAX (1024) limit:
	 * Bump shared_blk's h_refcount to EXT4_XATTR_REFCOUNT_MAX (1024),
	 * then set the same xattr on inode2. Cache lookup must see
	 * ref >= EXT4_XATTR_REFCOUNT_MAX, clear MBE_REUSABLE_B, and allocate
	 * a separate block instead of exceeding 1024.
	 */
	val100[0] = 'D';
	BHDR(&blk->bh)->h_refcount = cpu_to_le32(EXT4_XATTR_REFCOUNT_MAX);
	hash = le32_to_cpu(BHDR(&blk->bh)->h_hash);

	ret = ext4_xattr_set(inode2, EXT4_XATTR_INDEX_USER, "shared",
			     val100, sizeof(val100), XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test, EXT4_I(inode2)->i_file_acl, shared_blk);

	ce = mb_cache_entry_get(ctx->sbi.s_ea_block_cache, hash, shared_blk);
	KUNIT_ASSERT_NOT_NULL(test, ce);
	KUNIT_EXPECT_FALSE(test, test_bit(MBE_REUSABLE_B, &ce->e_flags));
	mb_cache_entry_put(ctx->sbi.s_ea_block_cache, ce);

	/*
	 * Removing the attribute on inode1 releases one reference on
	 * shared_blk (1024 -> 1023 = EXT4_XATTR_REFCOUNT_MAX - 1), which
	 * re-enables MBE_REUSABLE_B on the mb_cache entry.
	 */
	ret = ext4_xattr_set(inode1, EXT4_XATTR_INDEX_USER, "shared",
			     NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(BHDR(&blk->bh)->h_refcount),
			EXT4_XATTR_REFCOUNT_MAX - 1);

	ce = mb_cache_entry_get(ctx->sbi.s_ea_block_cache, hash, shared_blk);
	KUNIT_ASSERT_NOT_NULL(test, ce);
	KUNIT_EXPECT_TRUE(test, test_bit(MBE_REUSABLE_B, &ce->e_flags));
	mb_cache_entry_put(ctx->sbi.s_ea_block_cache, ce);
}

/*
 * Test 7: ext4_listxattr() across ibody and external block, and user/hurd/
 * trusted handler prefix and mount-option filtering.
 */
static void test_xattr_list_and_handlers(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct dentry *dentry = &ctx->root_dentry;
	char val100[100];
	char list_buf[128];
	char val_buf[16];
	ssize_t list_len;
	int ret;

	/* Handler get/set/list via user, hurd, and trusted handlers */
	ret = ext4_xattr_user_handler.set(&ext4_xattr_user_handler,
					  &nop_mnt_idmap, dentry, inode,
					  "ukey", "uval", 4, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_xattr_user_handler.get(&ext4_xattr_user_handler,
					  dentry, inode, "ukey",
					  val_buf, sizeof(val_buf));
	KUNIT_EXPECT_EQ(test, ret, 4);

	ret = ext4_xattr_hurd_handler.set(&ext4_xattr_hurd_handler,
					  &nop_mnt_idmap, dentry, inode,
					  "hkey", "hval", 4, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_xattr_hurd_handler.get(&ext4_xattr_hurd_handler,
					  dentry, inode, "hkey",
					  val_buf, sizeof(val_buf));
	KUNIT_EXPECT_EQ(test, ret, 4);

	/* Add a large attribute in external block via trusted handler */
	memset(val100, 'T', sizeof(val100));
	ret = ext4_xattr_trusted_handler.set(&ext4_xattr_trusted_handler,
					     &nop_mnt_idmap, dentry, inode,
					     "tkey", val100, sizeof(val100),
					     XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * With XATTR_USER set: "user.ukey\0" (10) + "gnu.hkey\0" (9) +
	 * (if CAP_SYS_ADMIN) "trusted.tkey\0" (13).
	 */
	list_len = ext4_listxattr(dentry, NULL, 0);
	KUNIT_EXPECT_GE(test, list_len, (ssize_t)19);

	/* Buffer too small by 1 byte returns -ERANGE */
	KUNIT_EXPECT_EQ(test,
			ext4_listxattr(dentry, list_buf, list_len - 1),
			(ssize_t)-ERANGE);

	memset(list_buf, 0, sizeof(list_buf));
	KUNIT_EXPECT_EQ(test,
			ext4_listxattr(dentry, list_buf, sizeof(list_buf)),
			list_len);
	KUNIT_EXPECT_MEMEQ(test, list_buf, "user.ukey\0gnu.hkey\0", 19);

	/*
	 * Clearing XATTR_USER mount option disables user and hurd handlers
	 * (-EOPNOTSUPP on get/set and filtered out of ext4_listxattr).
	 */
	clear_opt(&ctx->sb, XATTR_USER);
	KUNIT_EXPECT_FALSE(test, ext4_xattr_user_handler.list(dentry));
	KUNIT_EXPECT_FALSE(test, ext4_xattr_hurd_handler.list(dentry));
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_user_handler.get(&ext4_xattr_user_handler,
						    dentry, inode, "ukey",
						    val_buf, sizeof(val_buf)),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_user_handler.set(&ext4_xattr_user_handler,
						    &nop_mnt_idmap, dentry,
						    inode, "ukey", "x", 1, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_hurd_handler.get(&ext4_xattr_hurd_handler,
						    dentry, inode, "hkey",
						    val_buf, sizeof(val_buf)),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_hurd_handler.set(&ext4_xattr_hurd_handler,
						    &nop_mnt_idmap, dentry,
						    inode, "hkey", "x", 1, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ext4_listxattr(dentry, NULL, 0),
			capable(CAP_SYS_ADMIN) ? (ssize_t)13 : (ssize_t)0);
	set_opt(&ctx->sb, XATTR_USER);
}

/*
 * Test 8: Large xattr inodes (ea_inode feature) multi-block write/read,
 * 64-bit refcount & hash encoding, mb_cache deduplication across inodes,
 * reference decrement to 0, and ext4_evict_ea_inode().
 */
static void test_xattr_ea_inode_lifecycle_and_dedup(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode1 = &ctx->primary.ei.vfs_inode;
	struct inode *inode2 = &ctx->secondary.ei.vfs_inode;
	struct inode *ea_inode;
	struct ext4_inode *raw1 = (struct ext4_inode *)ctx->primary.raw_buf;
	struct ext4_xattr_entry *e1;
	char *large_val, *read_buf;
	const size_t large_len = 1500; /* Spans 2 1024-byte blocks */
	unsigned int ea_ino;
	u64 refcount;
	int i, ret;

	ext4_set_feature_ea_inode(&ctx->sb);
	ctx->sbi.s_ea_inode_cache = ext4_xattr_create_cache();
	KUNIT_ASSERT_NOT_NULL(test, ctx->sbi.s_ea_inode_cache);
	ext4_init_ea_inode_work(&ctx->sbi);

	large_val = kunit_kmalloc(test, large_len, GFP_KERNEL);
	read_buf = kunit_kzalloc(test, large_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, large_val);
	KUNIT_ASSERT_NOT_NULL(test, read_buf);
	for (i = 0; i < large_len; i++)
		large_val[i] = (char)('A' + (i % 26));

	/* Create 1500-byte xattr on inode1: stored in a new EA inode */
	ret = ext4_xattr_set(inode1, EXT4_XATTR_INDEX_USER, "big",
			     large_val, large_len, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	e1 = IFIRST(IHDR(inode1, raw1));
	ea_ino = le32_to_cpu(e1->e_value_inum);
	KUNIT_ASSERT_EQ(test, ea_ino, (unsigned int)TEST_FIRST_EA_INO);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(e1->e_value_offs), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(e1->e_value_size),
			(u32)large_len);

	ea_inode = &ctx->ea_inodes[0].ei.vfs_inode;
	refcount = ((u64)inode_get_ctime_sec(ea_inode) << 32) |
		   (u32)inode_peek_iversion_raw(ea_inode);
	KUNIT_EXPECT_EQ(test, refcount, 1ULL);
	KUNIT_EXPECT_EQ(test, ea_inode->i_nlink, 1U);

	/* Read back multi-block EA inode value via ext4_xattr_get() */
	ret = ext4_xattr_get(inode1, EXT4_XATTR_INDEX_USER, "big",
			     read_buf, large_len);
	KUNIT_ASSERT_EQ(test, ret, (int)large_len);
	KUNIT_EXPECT_MEMEQ(test, read_buf, large_val, large_len);

	/*
	 * Setting the identical 1500-byte value on inode2 deduplicates via
	 * s_ea_inode_cache and increments ea_inode's refcount to 2.
	 */
	ret = ext4_xattr_set(inode2, EXT4_XATTR_INDEX_USER, "big2",
			     large_val, large_len, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	refcount = ((u64)inode_get_ctime_sec(ea_inode) << 32) |
		   (u32)inode_peek_iversion_raw(ea_inode);
	KUNIT_EXPECT_EQ(test, refcount, 2ULL);

	/* Removing from inode1 drops refcount to 1; i_nlink remains 1 */
	ret = ext4_xattr_set(inode1, EXT4_XATTR_INDEX_USER, "big",
			     NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	refcount = ((u64)inode_get_ctime_sec(ea_inode) << 32) |
		   (u32)inode_peek_iversion_raw(ea_inode);
	KUNIT_EXPECT_EQ(test, refcount, 1ULL);
	KUNIT_EXPECT_EQ(test, ea_inode->i_nlink, 1U);

	/*
	 * Removing from inode2 drops refcount to 0 and clears i_nlink to 0.
	 * Also verify ext4_evict_ea_inode() removes it from s_ea_inode_cache.
	 */
	ret = ext4_xattr_set(inode2, EXT4_XATTR_INDEX_USER, "big2",
			     NULL, 0, XATTR_REPLACE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	refcount = ((u64)inode_get_ctime_sec(ea_inode) << 32) |
		   (u32)inode_peek_iversion_raw(ea_inode);
	KUNIT_EXPECT_EQ(test, refcount, 0ULL);
	KUNIT_EXPECT_EQ(test, ea_inode->i_nlink, 0U);

	ext4_evict_ea_inode(ea_inode);
}

/*
 * Test 9: EA inode hash verification (including legacy signed-char name hash
 * fallback), ext4_xattr_inode_iget() corruption checks, and Lustre-style
 * EA inode compatibility.
 */
static void test_xattr_ea_inode_validation_and_compat(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct inode *ea_inode;
	struct ext4_inode *raw = (struct ext4_inode *)ctx->primary.raw_buf;
	struct ext4_xattr_entry *e;
	static const char signed_name[] = "\x80\xfe";
	const size_t val_len = 1000;
	char *val, *buf;
	u32 val_hash, signed_e_hash = 0;
	__le32 orig_e_hash;
	int i, ret;

	ext4_set_feature_ea_inode(&ctx->sb);
	val = kunit_kmalloc(test, val_len, GFP_KERNEL);
	buf = kunit_kzalloc(test, val_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, val);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	memset(val, 'S', val_len);

	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     val, val_len, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	e = IFIRST(IHDR(inode, raw));
	ea_inode = &ctx->ea_inodes[0].ei.vfs_inode;
	orig_e_hash = e->e_hash;

	/*
	 * Compute the legacy signed-char entry hash for "\x80\xfe" and verify
	 * ext4_xattr_inode_verify_hashes() accepts it as a valid fallback.
	 */
	val_hash = (u32)inode_get_atime_sec(ea_inode);
	for (i = 0; i < (int)strlen(signed_name); i++) {
		signed_e_hash = (signed_e_hash << 5) ^
				(signed_e_hash >> 27) ^
				(signed char)signed_name[i];
	}
	signed_e_hash = (signed_e_hash << 16) ^
			(signed_e_hash >> 16) ^ val_hash;
	e->e_hash = cpu_to_le32(signed_e_hash);

	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, (int)val_len);

	/* Truly corrupted entry hash is rejected with -EFSCORRUPTED */
	e->e_hash = cpu_to_le32(signed_e_hash ^ 0x12345678U);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	e->e_hash = orig_e_hash;

	/* Corrupted value hash stored in ea_inode->i_atime is rejected */
	inode_set_atime(ea_inode, val_hash ^ 1, 0);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	inode_set_atime(ea_inode, val_hash, 0);

	/* Size mismatch between ea_inode->i_size and entry->e_value_size */
	i_size_write(ea_inode, val_len - 1);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	i_size_write(ea_inode, val_len);

	/* Invalid i_nlink (0 or 2) on active EA inode returns -EFSCORRUPTED */
	set_nlink(ea_inode, 2);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	set_nlink(ea_inode, 1);

	/* Self-referencing e_value_inum == parent->i_ino returns -EFSCORRUPTED */
	e->e_value_inum = cpu_to_le32(inode->i_ino);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	e->e_value_inum = cpu_to_le32(ea_inode->i_ino);

	/*
	 * Lustre-style old EA inode compatibility: when e_hash != ea_inode
	 * hash, but parent ino (stored in i_mtime) and generation (stored in
	 * i_generation) match parent, ext4_xattr_inode_iget() sets
	 * EXT4_STATE_LUSTRE_EA_INODE and forces ref_count = 1.
	 */
	e->e_hash = 0;
	inode->i_generation = 42;
	ea_inode->i_generation = 42;
	inode_set_mtime(ea_inode, inode->i_ino, 0);
	inode_set_ctime(ea_inode, 0, 0);
	inode_set_iversion_raw(ea_inode, 0);
	/* Even with initial ref_count = 0, Lustre compat sets ref = 1 */
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, signed_name,
			     buf, val_len);
	KUNIT_EXPECT_EQ(test, ret, (int)val_len);
	KUNIT_EXPECT_NE(test,
			ext4_test_inode_state(ea_inode,
					      EXT4_STATE_LUSTRE_EA_INODE),
			0);
}

/*
 * Test 10: ext4_get_inode_usage(), credit calculation, handle credit check,
 * ext4_expand_extra_isize_ea(), and ext4_xattr_delete_inode().
 */
static void test_xattr_credits_usage_expand_and_delete(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	struct ext4_inode *raw = (struct ext4_inode *)ctx->primary.raw_buf;
	journal_t *fake_journal;
	handle_t fake_handle = {
		.h_reserved = 1,
		.h_total_credits = 1,
	};
	const size_t large_len = 1000;
	char *large_val;
	char val28[28];
	char buf[64];
	qsize_t usage = 0;
	int credits_create, credits_replace, credits_out = -1;
	int ret;

	fake_journal = kunit_kzalloc(test, sizeof(*fake_journal), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fake_journal);
	fake_journal->j_revoke_records_per_block = 64;
	fake_handle.h_journal = fake_journal;

	large_val = kunit_kmalloc(test, large_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, large_val);
	memset(large_val, 'L', large_len);

	/*
	 * 1. Credit calculation and insufficient handle credits check
	 */
	credits_create = __ext4_xattr_set_credits(&ctx->sb, inode, NULL,
						  100, true);
	KUNIT_EXPECT_GT(test, credits_create, 0);

	ext4_set_feature_ea_inode(&ctx->sb);
	credits_create = __ext4_xattr_set_credits(&ctx->sb, inode, NULL,
						  1500, true);
	credits_replace = __ext4_xattr_set_credits(&ctx->sb, inode, NULL,
						   1500, false);
	KUNIT_EXPECT_GT(test, credits_replace, credits_create);

	/* Without a journal (s_journal == NULL), credits_out is 0 */
	ret = ext4_xattr_set_credits(inode, 1500, true, &credits_out);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, credits_out, 0);

	/* With s_journal set, ext4_xattr_set_credits() returns credits_create */
	ctx->sbi.s_journal = fake_journal;
	ret = ext4_xattr_set_credits(inode, 1500, true, &credits_out);
	ctx->sbi.s_journal = NULL;
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, credits_out, credits_create);

	/* Handle with only 1 buffer credit (< credits) fails with -ENOSPC */
	ret = ext4_xattr_set_handle(&fake_handle, inode, EXT4_XATTR_INDEX_USER,
				    "k", "v", 1, XATTR_CREATE);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);

	/*
	 * 2. ext4_expand_extra_isize_ea():
	 * Put "keep" (4B -> 24B total) and "evict" (28B -> 52B total) in
	 * ibody (88B usable, leaving 12B free).
	 * First expand i_extra_isize from 32 -> 40 (8 bytes <= 12B free):
	 * shifts ibody entries in-place without allocating an external block.
	 * Then expand i_extra_isize from 40 -> 72 (32 bytes > 4B free):
	 * "keep" (24 + 4 = 28 < 32) is too small alone, so "evict" (52 + 4 >= 32)
	 * is moved to an external xattr block while "keep" stays in ibody!
	 */
	ext4_clear_feature_ea_inode(&ctx->sb);
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "keep",
			     "1234", 4, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	memset(val28, 'E', sizeof(val28));
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "evict",
			     val28, sizeof(val28), XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/* In-place ibody shift (32 -> 40) */
	ret = ext4_expand_extra_isize_ea(inode, 40, raw, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_extra_isize, (u16)40);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/* Eviction of "evict" to external block (40 -> 72) */
	ret = ext4_expand_extra_isize_ea(inode, 72, raw, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_extra_isize, (u16)72);
	KUNIT_EXPECT_NE(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/* Both "keep" (in ibody) and "evict" (in external block) remain readable */
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_ibody_get(inode, EXT4_XATTR_INDEX_USER,
					     "keep", buf, sizeof(buf)),
			4);
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER,
				       "evict", buf, sizeof(buf)),
			28);

	/*
	 * 3. ext4_get_inode_usage() and ext4_xattr_delete_inode():
	 * Add a 1000-byte EA inode attribute ("large_ea") and check that
	 * ext4_get_inode_usage() accounts for 1 (parent inode) + 1 (EA inode)
	 * = 2 inodes, and ext4_xattr_delete_inode() frees the external block
	 * and drops the EA inode reference to 0.
	 */
	ext4_set_feature_ea_inode(&ctx->sb);
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "large_ea",
			     large_val, large_len, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	down_read(&EXT4_I(inode)->xattr_sem);
	ret = ext4_get_inode_usage(inode, &usage);
	up_read(&EXT4_I(inode)->xattr_sem);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, usage, (qsize_t)2);

	ret = ext4_xattr_delete_inode(NULL, inode, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);
	KUNIT_EXPECT_EQ(test, ctx->ea_inodes[0].ei.vfs_inode.i_nlink, 0U);
}

/*
 * Test 11: Error injection and rollback paths across inode location lookup,
 * external block allocation/getblk/bread/dirty_metadata, and EA inode
 * creation/write/lookup.
 */
static void test_xattr_error_injection(struct kunit *test)
{
	struct xattr_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->primary.ei.vfs_inode;
	const size_t large_len = 1000;
	char *large_val;
	char val100[100];
	char buf[128];
	unsigned int prev_free;
	int ret;

	large_val = kunit_kmalloc(test, large_len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, large_val);
	memset(large_val, 'F', large_len);
	memset(val100, 'B', sizeof(val100));

	/* 1. ext4_get_inode_loc() failure on set and get */
	ctx->fail_get_inode_loc = -EIO;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "k",
			     "v", 1, XATTR_CREATE);
	KUNIT_EXPECT_EQ(test, ret, -EIO);

	ext4_set_inode_state(inode, EXT4_STATE_XATTR);
	ret = ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "k",
			     buf, sizeof(buf));
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	ext4_clear_inode_state(inode, EXT4_STATE_XATTR);
	ctx->fail_get_inode_loc = 0;

	/* 2. ext4_mark_iloc_dirty() failure on set */
	ctx->fail_mark_iloc_dirty = -EIO;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "k",
			     "v", 1, XATTR_CREATE);
	ctx->fail_mark_iloc_dirty = 0;
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	/* Clean up the entry written to raw_buf before mark_iloc_dirty */
	KUNIT_ASSERT_EQ(test,
			ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "k",
				       NULL, 0, XATTR_REPLACE),
			0);

	/* 3. External block allocation failure (-ENOSPC) */
	ctx->fail_alloc_block = -ENOSPC;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "blk",
			     val100, sizeof(val100), XATTR_CREATE);
	ctx->fail_alloc_block = 0;
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/*
	 * 4. ext4_xattr_getblk() failure (-ENOMEM) after block allocation must
	 * free the newly allocated block via ext4_free_blocks().
	 */
	prev_free = ctx->free_block_calls;
	ctx->fail_xattr_getblk = true;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "blk",
			     val100, sizeof(val100), XATTR_CREATE);
	ctx->fail_xattr_getblk = false;
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	KUNIT_EXPECT_EQ(test, ctx->free_block_calls, prev_free + 1);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/* 5. ext4_handle_dirty_metadata() failure when creating external block */
	ctx->fail_dirty_metadata = -EIO;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "blk",
			     val100, sizeof(val100), XATTR_CREATE);
	ctx->fail_dirty_metadata = 0;
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_file_acl, 0ULL);

	/* Successfully create external block, then test ext4_sb_bread() failure */
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "blk",
			     val100, sizeof(val100), XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->fail_sb_bread = -EIO;
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "blk",
				       buf, sizeof(buf)),
			-EIO);
	KUNIT_EXPECT_EQ(test,
			ext4_listxattr(&ctx->root_dentry, buf, sizeof(buf)),
			(ssize_t)-EIO);
	KUNIT_EXPECT_EQ(test, ext4_xattr_delete_inode(NULL, inode, 0), -EIO);
	ctx->fail_sb_bread = 0;

	/*
	 * 6. EA inode block allocation failure in ext4_xattr_inode_write()
	 * must clean up the newly created EA inode (dropping its i_nlink to 0).
	 */
	ext4_set_feature_ea_inode(&ctx->sb);
	ctx->fail_alloc_block = -ENOSPC;
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "big_err",
			     large_val, large_len, XATTR_CREATE);
	ctx->fail_alloc_block = 0;
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_TRUE(test, ctx->ea_inodes[0].allocated);
	KUNIT_EXPECT_EQ(test, ctx->ea_inodes[0].ei.vfs_inode.i_nlink, 0U);

	/* 7. Successfully create EA inode, then test __ext4_iget() and ext4_getblk() failure */
	ret = ext4_xattr_set(inode, EXT4_XATTR_INDEX_USER, "big_ok",
			     large_val, large_len, XATTR_CREATE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->fail_iget = -EIO;
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "big_ok",
				       large_val, large_len),
			-EIO);
	ctx->fail_iget = 0;

	ctx->fail_getblk = -EIO;
	KUNIT_EXPECT_EQ(test,
			ext4_xattr_get(inode, EXT4_XATTR_INDEX_USER, "big_ok",
				       large_val, large_len),
			-EIO);
	ctx->fail_getblk = 0;
}

static struct kunit_case xattr_test_cases[] = {
	KUNIT_CASE(test_xattr_macros_and_locking),
	KUNIT_CASE_PARAM(test_xattr_check_inode, xattr_check_gen_params),
	KUNIT_CASE(test_xattr_ibody_basic_and_flags),
	KUNIT_CASE(test_xattr_ibody_replace_and_remove),
	KUNIT_CASE(test_xattr_external_block_and_check),
	KUNIT_CASE(test_xattr_block_dedup_and_cow),
	KUNIT_CASE(test_xattr_list_and_handlers),
	KUNIT_CASE(test_xattr_ea_inode_lifecycle_and_dedup),
	KUNIT_CASE(test_xattr_ea_inode_validation_and_compat),
	KUNIT_CASE(test_xattr_credits_usage_expand_and_delete),
	KUNIT_CASE(test_xattr_error_injection),
	{}
};

static struct kunit_suite xattr_test_suite = {
	.name = "ext4_xattr_test",
	.init = xattr_test_init,
	.exit = xattr_test_exit,
	.test_cases = xattr_test_cases,
};

kunit_test_suites(&xattr_test_suite);

MODULE_LICENSE("GPL");
