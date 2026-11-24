// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 inline data operations (fs/ext4/inline.c)
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/iversion.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "truncate.h"
#include "namei.h"

#define TEST_INODE_SIZE		256
#define TEST_EXTRA_ISIZE	32
#define TEST_BLOCK_SIZE		1024
#define TEST_BLOCK_BITS		10
#define EXT4_MIN_INLINE_DATA_SIZE	((sizeof(__le32) * EXT4_N_BLOCKS))
#define EXT4_INLINE_DOTDOT_SIZE		4
/*
 * For a 256-byte inode with 32-byte i_extra_isize:
 *   EXT4_GOOD_OLD_INODE_SIZE (128) + 32 = 160 bytes fixed fields
 *   Remaining ibody space = 96 bytes
 *   4-byte xattr magic + 20-byte system.data entry + 4-byte gap = 28 bytes
 *   Available xattr value space = 68 bytes
 *   Plus 60 bytes in i_block (EXT4_MIN_INLINE_DATA_SIZE) = 128 bytes max inline size.
 */
#define TEST_MAX_INLINE_SIZE	128

#define TEST_INO		12
#define TEST_PARENT_INO		2
#define TEST_PBLK		100

struct inline_test_ctx {
	struct super_block sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct block_device bdev;
	struct address_space bd_mapping;
	struct buffer_head sb_bh;
	struct ext4_inode_info ei;
	struct buffer_head iloc_bh;
	u8 raw_inode_buf[TEST_BLOCK_SIZE];
	struct buffer_head data_bh;
	u8 data_block_buf[PAGE_SIZE];
	int get_inode_loc_err;
	int map_blocks_ret;
	ext4_fsblk_t map_pblk;
	unsigned int map_flags;
	bool getblk_fail;
	int dirty_metadata_err;
	int dirty_metadata_fail_on_call;
	int dirty_metadata_calls;
	int da_get_block_prep_err;
	int journal_mode;
};

static struct inline_test_ctx *t_ctx;

#ifdef CONFIG_QUOTA
static struct dquot __rcu **inline_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations inline_test_sops = {
#ifdef CONFIG_QUOTA
	.get_dquots	= inline_test_get_dquots,
#endif
};

static bool test_release_folio(struct folio *folio, gfp_t wait)
{
	return try_to_free_buffers(folio);
}

static const struct address_space_operations test_inline_aops = {
	.dirty_folio		= noop_dirty_folio,
	.invalidate_folio	= block_invalidate_folio,
	.release_folio		= test_release_folio,
};

/* --- Static Stubs --- */

static int stub_get_inode_loc(struct inode *inode, struct ext4_iloc *iloc)
{
	if (t_ctx->get_inode_loc_err)
		return t_ctx->get_inode_loc_err;

	memset(iloc, 0, sizeof(*iloc));
	atomic_inc(&t_ctx->iloc_bh.b_count);
	iloc->bh = &t_ctx->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int stub_handle_dirty_metadata(const char *where, unsigned int line,
				      handle_t *handle, struct inode *inode,
				      struct buffer_head *bh)
{
	t_ctx->dirty_metadata_calls++;
	if (t_ctx->dirty_metadata_fail_on_call > 0 &&
	    t_ctx->dirty_metadata_calls == t_ctx->dirty_metadata_fail_on_call)
		return t_ctx->dirty_metadata_err;
	if (t_ctx->dirty_metadata_fail_on_call == 0 &&
	    t_ctx->dirty_metadata_err)
		return t_ctx->dirty_metadata_err;
	set_buffer_dirty(bh);
	return 0;
}

static int stub_mark_iloc_dirty(handle_t *handle, struct inode *inode,
				struct ext4_iloc *iloc)
{
	brelse(iloc->bh);
	return 0;
}

static int stub_mark_inode_dirty(handle_t *handle, struct inode *inode,
				 const char *func, unsigned int line)
{
	return 0;
}

static int stub_map_blocks(handle_t *handle, struct inode *inode,
			   struct ext4_map_blocks *map, int flags)
{
	if (t_ctx->map_blocks_ret < 0)
		return t_ctx->map_blocks_ret;

	map->m_pblk = t_ctx->map_pblk;
	map->m_flags = t_ctx->map_flags;
	if (t_ctx->map_blocks_ret > 0)
		map->m_len = t_ctx->map_blocks_ret;
	return t_ctx->map_blocks_ret;
}

static struct buffer_head *stub_inline_getblk(struct super_block *sb,
					      ext4_fsblk_t block)
{
	if (t_ctx->getblk_fail)
		return NULL;

	atomic_inc(&t_ctx->data_bh.b_count);
	t_ctx->data_bh.b_blocknr = block;
	return &t_ctx->data_bh;
}

static int stub_da_get_block_prep(struct inode *inode, sector_t iblock,
				  struct buffer_head *bh, int create)
{
	if (t_ctx->da_get_block_prep_err)
		return t_ctx->da_get_block_prep_err;

	set_buffer_new(bh);
	set_buffer_delay(bh);
	bh->b_blocknr = 0;
	bh->b_size = inode->i_sb->s_blocksize;
	return 0;
}

static int stub_inode_journal_mode(struct inode *inode)
{
	return t_ctx->journal_mode;
}

/* --- Helpers --- */

static void *raw_inline_i_block(struct inline_test_ctx *ctx)
{
	struct ext4_inode *raw_inode = (struct ext4_inode *)ctx->raw_inode_buf;

	return (void *)raw_inode->i_block;
}

static void *raw_inline_xattr_pos(struct inline_test_ctx *ctx)
{
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_inode *raw_inode = (struct ext4_inode *)ctx->raw_inode_buf;
	struct ext4_xattr_ibody_header *header = IHDR(inode, raw_inode);
	struct ext4_xattr_entry *entry;

	entry = (struct ext4_xattr_entry *)((void *)raw_inode +
					    ctx->ei.i_inline_off);
	return (void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs);
}

static void reset_inode_state(struct inline_test_ctx *ctx, umode_t mode)
{
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_inode *raw_inode;

	if (!list_empty(&ctx->ei.i_orphan))
		list_del_init(&ctx->ei.i_orphan);

	truncate_inode_pages(&inode->i_data, 0);

	memset(ctx->raw_inode_buf, 0, sizeof(ctx->raw_inode_buf));
	memset(ctx->data_block_buf, 0, sizeof(ctx->data_block_buf));
	memset(ctx->ei.i_data, 0, sizeof(ctx->ei.i_data));

	raw_inode = (struct ext4_inode *)ctx->raw_inode_buf;
	raw_inode->i_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);

	ctx->ei.i_extra_isize = TEST_EXTRA_ISIZE;
	ctx->ei.i_inline_off = 0;
	ctx->ei.i_inline_size = 0;
	ctx->ei.i_flags = 0;
	ext4_clear_state_flags(&ctx->ei);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ctx->ei.i_disksize = 0;

	inode->i_mode = mode;
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode_set_iversion(inode, 1);
	inode_state_set_raw(inode, I_DIRTY);
}

static int create_inline_file(struct inode *inode, const void *data, size_t len)
{
	struct folio *folio = NULL;
	void *kaddr;
	int ret;

	inode_lock(inode);
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, len,
					    &folio);
	if (ret != 1) {
		inode_unlock(inode);
		return ret < 0 ? ret : -ENOSPC;
	}

	if (data && len) {
		kaddr = kmap_local_folio(folio, 0);
		memcpy(kaddr, data, len);
		kunmap_local(kaddr);
	}

	ret = ext4_write_inline_data_end(inode, 0, len, len, folio);
	inode_unlock(inode);
	if (ret < 0)
		return ret;

	i_size_write(inode, len);
	EXT4_I(inode)->i_disksize = len;
	return 0;
}

static int inline_test_init(struct kunit *test)
{
	struct inline_test_ctx *ctx;
	struct inode *inode;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	t_ctx = ctx;
	test->priv = ctx;

	/* Superblock and ext4_sb_info setup */
	ctx->sb.s_blocksize = TEST_BLOCK_SIZE;
	ctx->sb.s_blocksize_bits = TEST_BLOCK_BITS;
	ctx->sb.s_maxbytes = MAX_LFS_FILESIZE;
	ctx->sb.s_time_gran = 1;
	ctx->sb.s_bdi = &noop_backing_dev_info;
	ctx->sb.s_fs_info = &ctx->sbi;
	ctx->sb.s_op = &inline_test_sops;
#ifdef CONFIG_FS_ENCRYPTION
	ctx->sb.s_cop = &ext4_cryptops;
#endif
	snprintf(ctx->sb.s_id, sizeof(ctx->sb.s_id), "ext4-inline-test");
	init_rwsem(&ctx->sb.s_umount);

	ctx->bdev.bd_mapping = &ctx->bd_mapping;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	ctx->sb.s_bdev = &ctx->bdev;

	ctx->es.s_feature_incompat =
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_INLINE_DATA |
			    EXT4_FEATURE_INCOMPAT_EXTENTS |
			    EXT4_FEATURE_INCOMPAT_FILETYPE);
	ctx->es.s_first_data_block = cpu_to_le32(1);
	ctx->es.s_inodes_count = cpu_to_le32(1024);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_inode_size = cpu_to_le16(TEST_INODE_SIZE);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	ctx->sbi.s_sbh = &ctx->sb_bh;

	ctx->sbi.s_sb = &ctx->sb;
	ctx->sbi.s_es = &ctx->es;
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;
	ctx->sbi.s_want_extra_isize = TEST_EXTRA_ISIZE;
	ctx->sbi.s_cluster_bits = 0;
	ctx->sbi.s_cluster_ratio = 1;
	INIT_LIST_HEAD(&ctx->sbi.s_orphan);
	mutex_init(&ctx->sbi.s_orphan_lock);
	set_opt(&ctx->sb, ERRORS_CONT);
	spin_lock_init(&ctx->sbi.s_bdev_wb_lock);
	spin_lock_init(&ctx->sbi.s_error_lock);
	spin_lock_init(&ctx->sbi.s_md_lock);
	ratelimit_state_init(&ctx->sbi.s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&ctx->sbi.s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&ctx->sbi.s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	/* Mock buffer_head for raw inode (iloc.bh) */
	ctx->iloc_bh.b_data = (char *)ctx->raw_inode_buf;
	ctx->iloc_bh.b_size = TEST_BLOCK_SIZE;
	ctx->iloc_bh.b_blocknr = 10;
	ctx->iloc_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->iloc_bh.b_count, 1);
	set_buffer_uptodate(&ctx->iloc_bh);
	set_buffer_mapped(&ctx->iloc_bh);

	/* Mock buffer_head for allocated data block (data_bh) */
	ctx->data_bh.b_data = (char *)ctx->data_block_buf;
	ctx->data_bh.b_size = TEST_BLOCK_SIZE;
	ctx->data_bh.b_blocknr = TEST_PBLK;
	ctx->data_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->data_bh.b_count, 1);
	set_buffer_uptodate(&ctx->data_bh);
	set_buffer_mapped(&ctx->data_bh);

	/* Default stub behavior */
	ctx->map_blocks_ret = 1;
	ctx->map_pblk = TEST_PBLK;
	ctx->map_flags = EXT4_MAP_MAPPED;
	ctx->journal_mode = EXT4_INODE_WRITEBACK_DATA_MODE;

	/* Initialize mock ext4_inode_info and embedded vfs_inode */
	inode = &ctx->ei.vfs_inode;
	inode->i_sb = &ctx->sb;
	inode->i_ino = TEST_INO;
	inode->i_blkbits = TEST_BLOCK_BITS;
	set_nlink(inode, 1);
	atomic_set(&inode->i_count, 1);
	spin_lock_init(&inode->i_lock);
	i_size_ordered_init(inode);
	init_rwsem(&inode->i_rwsem);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	INIT_HLIST_NODE(&inode->i_hash);
	inode_state_set_raw(inode, I_DIRTY);

	address_space_init_once(&inode->i_data);
	inode->i_data.host = inode;
	inode->i_data.a_ops = &test_inline_aops;
	mapping_set_gfp_mask(&inode->i_data, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&inode->i_data.invalidate_lock);
	inode->i_mapping = &inode->i_data;

	init_rwsem(&ctx->ei.xattr_sem);
	init_rwsem(&ctx->ei.i_data_sem);
	spin_lock_init(&ctx->ei.i_raw_lock);
	spin_lock_init(&ctx->ei.i_block_reservation_lock);
	INIT_LIST_HEAD(&ctx->ei.i_orphan);
	ctx->ei.i_prealloc_node = RB_ROOT;
	rwlock_init(&ctx->ei.i_prealloc_lock);
	ext4_es_init_tree(&ctx->ei.i_es_tree);
	rwlock_init(&ctx->ei.i_es_lock);
	INIT_LIST_HEAD(&ctx->ei.i_es_list);
	ctx->ei.i_es_all_nr = 0;
	ctx->ei.i_es_shk_nr = 0;

	reset_inode_state(ctx, S_IFREG | 0644);

	/* Activate static stubs */
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   stub_get_inode_loc);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   stub_handle_dirty_metadata);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   stub_mark_iloc_dirty);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   stub_mark_inode_dirty);
	kunit_activate_static_stub(test, ext4_map_blocks,
				   stub_map_blocks);
	kunit_activate_static_stub(test, ext4_inline_getblk,
				   stub_inline_getblk);
	kunit_activate_static_stub(test, ext4_da_get_block_prep,
				   stub_da_get_block_prep);
	kunit_activate_static_stub(test, ext4_inode_journal_mode,
				   stub_inode_journal_mode);

	return 0;
}

static void inline_test_exit(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;

	if (current->journal_info)
		ext4_journal_stop(ext4_journal_current_handle());

	if (ctx) {
		if (!list_empty(&ctx->ei.i_orphan))
			list_del_init(&ctx->ei.i_orphan);
		truncate_inode_pages(&ctx->ei.vfs_inode.i_data, 0);
	}
	t_ctx = NULL;
}

/* =========================================================================
 * 1. Capacity and Lookup Tests (ext4_get_max_inline_size /
 *    ext4_find_inline_data_nolock)
 * =========================================================================
 */

static void test_max_inline_size_variants(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	int ret;

	/* 1. Fresh 256-byte inode with 32-byte extra_isize -> 128 bytes */
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode),
			TEST_MAX_INLINE_SIZE);

	/* 2. Inode with i_extra_isize == 0 -> 0 */
	ctx->ei.i_extra_isize = 0;
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode), 0);
	ctx->ei.i_extra_isize = TEST_EXTRA_ISIZE;

	/* 3. Old 128-byte inode size (no xattr space) -> 0 */
	ctx->sbi.s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode), 0);
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;

	/* 4. ext4_get_inode_loc error -> 0 */
	ctx->get_inode_loc_err = -EIO;
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode), 0);
	ctx->get_inode_loc_err = 0;

	/* 5. With a user xattr in ibody reducing free space */
	{
		struct ext4_xattr_ibody_find is = {
			.s = { .not_found = -ENODATA, },
		};
		struct ext4_xattr_info i = {
			.name_index = EXT4_XATTR_INDEX_USER,
			.name = "foo",
			.value = "12345678",
			.value_len = 8,
		};

		ret = ext4_get_inode_loc(inode, &is.iloc);
		KUNIT_ASSERT_EQ(test, ret, 0);
		ret = ext4_xattr_ibody_find(inode, &i, &is);
		KUNIT_ASSERT_EQ(test, ret, 0);
		ret = ext4_xattr_ibody_set(NULL, inode, &i, &is);
		KUNIT_ASSERT_EQ(test, ret, 0);
		brelse(is.iloc.bh);
	}
	/*
	 * "foo" entry takes EXT4_XATTR_LEN(3) = 20 bytes + EXT4_XATTR_SIZE(8) = 8
	 * Total 28 bytes consumed in ibody -> max inline size drops from 128 to 100.
	 */
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode),
			TEST_MAX_INLINE_SIZE - 28);

	/* 6. When inline data already exists in system.data, its space counts as usable */
	reset_inode_state(ctx, S_IFREG | 0644);
	{
		char buf[80];

		memset(buf, 'A', sizeof(buf));
		ret = create_inline_file(inode, buf, sizeof(buf));
		KUNIT_ASSERT_EQ(test, ret, 0);
	}
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode),
			TEST_MAX_INLINE_SIZE);

	/*
	 * 7. When ibody free space is smaller than EXT4_XATTR_LEN(strlen("data"))
	 * (20 bytes), get_max_inline_xattr_value_size returns 0 -> max is 0.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	{
		struct ext4_xattr_ibody_find is = {
			.s = { .not_found = -ENODATA, },
		};
		char val[60];
		struct ext4_xattr_info i = {
			.name_index = EXT4_XATTR_INDEX_USER,
			.name = "bigattr",
			.value = val,
			.value_len = sizeof(val),
		};

		memset(val, 'x', sizeof(val));
		ret = ext4_get_inode_loc(inode, &is.iloc);
		KUNIT_ASSERT_EQ(test, ret, 0);
		ret = ext4_xattr_ibody_find(inode, &i, &is);
		KUNIT_ASSERT_EQ(test, ret, 0);
		ret = ext4_xattr_ibody_set(NULL, inode, &i, &is);
		KUNIT_ASSERT_EQ(test, ret, 0);
		brelse(is.iloc.bh);
	}
	KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode), 0);

	/*
	 * 8. Corrupted xattr entry in ibody (e_name_len overflows end of inode)
	 * -> get_max_inline_xattr_value_size returns 0 -> total max is 0.
	 */
	{
		struct ext4_inode *raw_inode =
			(struct ext4_inode *)ctx->raw_inode_buf;
		struct ext4_xattr_ibody_header *header = IHDR(inode, raw_inode);
		struct ext4_xattr_entry *entry = IFIRST(header);

		entry->e_name_len = 250;
		KUNIT_EXPECT_EQ(test, ext4_get_max_inline_size(inode), 0);
	}
}

static void test_find_inline_data_nolock(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_iloc iloc = { 0 };
	char buf[90];
	int ret;

	/* 1. i_extra_isize == 0 -> returns 0 immediately without setting inline state */
	ctx->ei.i_extra_isize = 0;
	ret = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	brelse(iloc.bh);
	ctx->ei.i_extra_isize = TEST_EXTRA_ISIZE;

	/* 2. ext4_get_inode_loc error -> propagates error */
	ctx->get_inode_loc_err = -EIO;
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	ctx->get_inode_loc_err = 0;

	/* 3. Inode without EXT4_STATE_XATTR -> returns 0, inline_size == 0 */
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 0);

	/* 4. Inode with EXT4_STATE_XATTR set, but no system.data xattr -> 0 */
	ext4_set_inode_state(inode, EXT4_STATE_XATTR);
	{
		struct ext4_inode *raw_inode =
			(struct ext4_inode *)ctx->raw_inode_buf;
		struct ext4_xattr_ibody_header *header = IHDR(inode, raw_inode);

		header->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
	}
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 0);

	/* 5. Inode with 30-byte inline file (0-byte system.data xattr) */
	reset_inode_state(ctx, S_IFREG | 0644);
	memset(buf, 'B', sizeof(buf));
	ret = create_inline_file(inode, buf, 30);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->ei.i_inline_off = 0;
	ctx->ei.i_inline_size = 0;
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_NE(test, ctx->ei.i_inline_off, 0);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));

	/* 6. Inode with 90-byte inline file (30-byte system.data xattr) */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, 90);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->ei.i_inline_off = 0;
	ctx->ei.i_inline_size = 0;
	ret = ext4_find_inline_data_nolock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 90);
	KUNIT_EXPECT_GT(test, ctx->ei.i_inline_off,
			EXT4_GOOD_OLD_INODE_SIZE + TEST_EXTRA_ISIZE);

	/* 7. Corrupted system.data entry with non-zero e_value_inum -> -EFSCORRUPTED */
	{
		struct ext4_inode *raw_inode =
			(struct ext4_inode *)ctx->raw_inode_buf;
		struct ext4_xattr_ibody_header *header = IHDR(inode, raw_inode);
		struct ext4_xattr_entry *entry = IFIRST(header);

		entry->e_value_inum = cpu_to_le32(99);
		ret = ext4_find_inline_data_nolock(inode);
		KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	}
}

/* =========================================================================
 * 2. Inline File Write & Destroy Lifecycle Tests
 * =========================================================================
 */

static void test_write_and_destroy_inline_data(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct ext4_iloc iloc = { 0 };
	struct folio *folio = NULL;
	handle_t *handle;
	char pattern[100];
	char verify_buf[100];
	void *kaddr;
	int i, ret;

	for (i = 0; i < sizeof(pattern); i++)
		pattern[i] = (char)('A' + (i % 26));

	/*
	 * Step 1: Write 20 bytes into a brand-new inode (fits in i_block).
	 */
	inode_lock(inode);
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 20,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, folio);
	KUNIT_EXPECT_TRUE(test, folio_test_locked(folio));
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size,
			(u16)EXT4_MIN_INLINE_DATA_SIZE);

	kaddr = kmap_local_folio(folio, 0);
	memcpy(kaddr, pattern, 20);
	kunmap_local(kaddr);

	ret = ext4_write_inline_data_end(inode, 0, 20, 20, folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 20);
	KUNIT_EXPECT_NULL(test, current->journal_info);
	i_size_write(inode, 20);
	ctx->ei.i_disksize = 20;

	/* Verify i_block contents */
	KUNIT_EXPECT_MEMEQ(test, raw_inline_i_block(ctx), pattern, 20);

	/*
	 * Step 2: Expand from 20 bytes to 90 bytes (crosses 60-byte boundary
	 * into ibody system.data xattr via ext4_update_inline_data).
	 */
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 20, 70,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, folio);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 90);

	/* Verify existing first 20 bytes were loaded into the folio by prepare_inline_data */
	kaddr = kmap_local_folio(folio, 0);
	KUNIT_EXPECT_MEMEQ(test, kaddr, pattern, 20);
	memcpy((char *)kaddr + 20, pattern + 20, 70);
	kunmap_local(kaddr);

	ret = ext4_write_inline_data_end(inode, 20, 70, 70, folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 70);
	i_size_write(inode, 90);
	ctx->ei.i_disksize = 90;

	/*
	 * Step 3: Overwrite 15 bytes purely within the xattr region
	 * (pos = 65, len = 15 -> pos >= EXT4_MIN_INLINE_DATA_SIZE).
	 */
	memset(pattern + 65, 'Z', 15);
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 65, 15,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	kaddr = kmap_local_folio(folio, 0);
	memcpy((char *)kaddr + 65, pattern + 65, 15);
	kunmap_local(kaddr);
	ret = ext4_write_inline_data_end(inode, 65, 15, 15, folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 15);

	/* Verify both i_block (first 60B) and ibody xattr (remaining 30B) */
	memcpy(verify_buf, raw_inline_i_block(ctx), EXT4_MIN_INLINE_DATA_SIZE);
	memcpy(verify_buf + EXT4_MIN_INLINE_DATA_SIZE,
	       raw_inline_xattr_pos(ctx), 30);
	KUNIT_EXPECT_MEMEQ(test, verify_buf, pattern, 90);

	/*
	 * Step 4: Destroy inline data on a regular file -> clears inline state
	 * and initializes extent tree (EXT4_INODE_EXTENTS).
	 */
	handle = ext4_journal_start(inode, EXT4_HT_INODE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_get_inode_loc(inode, &iloc);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4_destroy_inline_data(handle, inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_off, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 0);
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	brelse(iloc.bh);

	/* Calling destroy again when inline_off == 0 is a no-op returning 0 */
	ret = ext4_destroy_inline_data(handle, inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ext4_journal_stop(handle);

	/*
	 * Step 5: Initial creation directly > 60 bytes (75 bytes) on a non-extent
	 * mode inode (e.g. S_IFIFO) -> destroy does NOT set EXT4_INODE_EXTENTS.
	 */
	reset_inode_state(ctx, S_IFIFO | 0644);
	ret = create_inline_file(inode, pattern, 75);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 75);

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_destroy_inline_data(handle, inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	ext4_journal_stop(handle);
}

static void test_write_inline_short_copy_and_edge_cases(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct folio *folio = NULL;
	char buf[80];
	void *kaddr;
	int ret;

	memset(buf, 'M', sizeof(buf));
	ret = create_inline_file(inode, buf, 20);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * 1. Short write (copied < len) when expanding from 20B to 80B:
	 * prepare_inline_data expands xattr to 80B, but only 10 bytes are
	 * copied (pos=20, len=60, copied=10 -> pos+copied=30 > i_size=20).
	 * ext4_write_inline_data_end adds to orphan, updates i_disksize to 30,
	 * and calls ext4_truncate_failed_write -> shrinks inline data back to
	 * 60B (i_block only).
	 */
	inode_lock(inode);
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 20, 60,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 80);

	kaddr = kmap_local_folio(folio, 0);
	memcpy((char *)kaddr + 20, "0123456789", 10);
	kunmap_local(kaddr);

	ret = ext4_write_inline_data_end(inode, 20, 60, 10, folio);
	KUNIT_EXPECT_EQ(test, ret, 10);
	KUNIT_EXPECT_EQ(test, i_size_read(inode), 30);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size,
			(u16)EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei.i_orphan));
	inode_unlock(inode);

	/*
	 * 2. Short write (copied < len) when folio is NOT uptodate ->
	 * copied is forced to 0.
	 */
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 30, 10,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	folio_clear_uptodate(folio);
	ret = ext4_write_inline_data_end(inode, 30, 10, 5, folio);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, i_size_read(inode), 30);
	inode_unlock(inode);

	/*
	 * 3. Emergency shutdown flag (EXT4_FLAGS_SHUTDOWN):
	 *   3a. Set before ext4_try_to_write_inline_data -> ext4_journal_start
	 *       fails with -EIO.
	 *   3b. Set after ext4_try_to_write_inline_data -> ext4_write_inline_data
	 *       returns early without modifying inline data.
	 */
	set_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 10,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);

	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 10,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	kaddr = kmap_local_folio(folio, 0);
	memcpy(kaddr, "XXXXXXXXXX", 10);
	kunmap_local(kaddr);
	set_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);
	ret = ext4_write_inline_data_end(inode, 0, 10, 10, folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 10);
	/* i_block should still have 'M's because shutdown skipped ext4_write_inline_data */
	KUNIT_EXPECT_MEMEQ(test, raw_inline_i_block(ctx), buf, 10);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &ctx->sbi.s_ext4_flags);

	/*
	 * 4. Race condition where EXT4_INODE_INLINE_DATA is cleared before
	 * ext4_write_inline_data_end runs -> skips inline copy and returns 0.
	 */
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 10,
					    &folio);
	KUNIT_ASSERT_EQ(test, ret, 1);
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	ret = ext4_write_inline_data_end(inode, 0, 10, 10, folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);

	/*
	 * 5. Corruption detection in ext4_create_inline_data when system.data
	 * already exists on an inode with i_inline_off == 0 (e.g. corrupted
	 * e_value_inum so ext4_find_inline_data_nolock leaves i_inline_off == 0).
	 */
	{
		struct ext4_inode *raw_inode =
			(struct ext4_inode *)ctx->raw_inode_buf;
		struct ext4_xattr_ibody_header *header = IHDR(inode, raw_inode);
		struct ext4_xattr_entry *entry = IFIRST(header);

		entry->e_value_inum = cpu_to_le32(1);
		ctx->ei.i_inline_off = 0;
		ctx->ei.i_inline_size = 0;
		ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);
		ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
		folio = NULL;
		ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0,
						    10, &folio);
		KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	}

	/*
	 * 6. Corruption detection in ext4_update_inline_data when inline state
	 * claims inline data exists, but system.data xattr was wiped.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ctx->ei.i_inline_off = 164;
	ctx->ei.i_inline_size = EXT4_MIN_INLINE_DATA_SIZE;
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 80,
					    &folio);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
}

/* =========================================================================
 * 3. Reading Inline Data, Symlinks, and iomap Tests
 * =========================================================================
 */

static void test_readpage_inline_link_and_iomap(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct folio *folio;
	struct iomap iomap = { 0 };
	char content[85];
	const char *short_link = "/usr/bin/python3";
	char long_link[85];
	char *target;
	void *kaddr;
	int i, ret;

	for (i = 0; i < sizeof(content); i++)
		content[i] = (char)('0' + (i % 10));

	/* 1. ext4_readpage_inline and ext4_inline_data_iomap on non-inline inode -> -EAGAIN */
	folio = __filemap_get_folio(&inode->i_data, 0,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				    GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	ret = ext4_readpage_inline(inode, folio);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	folio_unlock(folio);
	folio_put(folio);

	ret = ext4_inline_data_iomap(inode, &iomap);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);

	/* 2. Create 85-byte inline file and test ext4_readpage_inline on index 0 */
	truncate_inode_pages(&inode->i_data, 0);
	ret = create_inline_file(inode, content, sizeof(content));
	KUNIT_ASSERT_EQ(test, ret, 0);
	truncate_inode_pages(&inode->i_data, 0);

	folio = __filemap_get_folio(&inode->i_data, 0,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				    GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	ret = ext4_readpage_inline(inode, folio);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, folio_test_uptodate(folio));
	KUNIT_EXPECT_FALSE(test, folio_test_locked(folio));

	kaddr = kmap_local_folio(folio, 0);
	KUNIT_EXPECT_MEMEQ(test, kaddr, content, sizeof(content));
	/* Verify zero-padding past inline_size */
	KUNIT_EXPECT_EQ(test, ((char *)kaddr)[sizeof(content)], 0);
	KUNIT_EXPECT_EQ(test, ((char *)kaddr)[PAGE_SIZE - 1], 0);
	kunmap_local(kaddr);
	folio_put(folio);

	/* 3. ext4_readpage_inline on folio->index > 0 zeros the folio and marks uptodate */
	folio = __filemap_get_folio(&inode->i_data, 1,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				    GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0xff, PAGE_SIZE);
	kunmap_local(kaddr);

	ret = ext4_readpage_inline(inode, folio);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, folio_test_uptodate(folio));
	kaddr = kmap_local_folio(folio, 0);
	KUNIT_EXPECT_EQ(test, ((unsigned char *)kaddr)[0], 0);
	kunmap_local(kaddr);
	folio_put(folio);

	/* 4. ext4_readpage_inline when ext4_get_inode_loc fails -> propagates error */
	truncate_inode_pages(&inode->i_data, 0);
	ctx->get_inode_loc_err = -EIO;
	folio = __filemap_get_folio(&inode->i_data, 0,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				    GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
	ret = ext4_readpage_inline(inode, folio);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	folio_put(folio);
	ctx->get_inode_loc_err = 0;

	/*
	 * 5. ext4_readpage_inline when i_inline_size > PAGE_SIZE -> -EFSCORRUPTED.
	 * i_inline_size is a u16, so it can only exceed PAGE_SIZE when
	 * PAGE_SIZE < U16_MAX (e.g. 4KB, 8KB, 16KB, or 32KB base pages).
	 */
	if (PAGE_SIZE < U16_MAX) {
		truncate_inode_pages(&inode->i_data, 0);
		ctx->ei.i_inline_size = U16_MAX;
		i_size_write(inode, U16_MAX);
		folio = __filemap_get_folio(&inode->i_data, 0,
					    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
					    GFP_KERNEL);
		KUNIT_ASSERT_FALSE(test, IS_ERR(folio));
		ret = ext4_readpage_inline(inode, folio);
		KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
		folio_put(folio);
		ctx->ei.i_inline_size = sizeof(content);
		i_size_write(inode, sizeof(content));
	}

	/* 6. ext4_inline_data_iomap populates IOMAP_INLINE with inline address/len */
	memset(&iomap, 0, sizeof(iomap));
	ret = ext4_inline_data_iomap(inode, &iomap);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, iomap.type, (u16)IOMAP_INLINE);
	KUNIT_EXPECT_EQ(test, iomap.offset, 0ULL);
	KUNIT_EXPECT_EQ(test, iomap.length, (u64)sizeof(content));
	KUNIT_EXPECT_EQ(test, iomap.addr,
			((u64)ctx->iloc_bh.b_blocknr << TEST_BLOCK_BITS) +
			offsetof(struct ext4_inode, i_block));

	/* 7. ext4_inline_data_iomap error when ext4_get_inode_loc fails */
	ctx->get_inode_loc_err = -EIO;
	ret = ext4_inline_data_iomap(inode, &iomap);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	ctx->get_inode_loc_err = 0;

	/* 8. ext4_read_inline_link: short symlink (in i_block) and long symlink (in xattr) */
	reset_inode_state(ctx, S_IFLNK | 0777);
	ret = create_inline_file(inode, short_link, strlen(short_link));
	KUNIT_ASSERT_EQ(test, ret, 0);

	target = ext4_read_inline_link(inode);
	KUNIT_ASSERT_FALSE(test, IS_ERR(target));
	KUNIT_EXPECT_STREQ(test, target, short_link);
	kfree(target);

	reset_inode_state(ctx, S_IFLNK | 0777);
	memset(long_link, 'a', sizeof(long_link) - 1);
	long_link[sizeof(long_link) - 1] = '\0';
	ret = create_inline_file(inode, long_link, strlen(long_link));
	KUNIT_ASSERT_EQ(test, ret, 0);

	target = ext4_read_inline_link(inode);
	KUNIT_ASSERT_FALSE(test, IS_ERR(target));
	KUNIT_EXPECT_STREQ(test, target, long_link);
	kfree(target);

	/* ext4_read_inline_link error when ext4_get_inode_loc fails */
	ctx->get_inode_loc_err = -EIO;
	target = ext4_read_inline_link(inode);
	KUNIT_EXPECT_TRUE(test, IS_ERR(target));
	KUNIT_EXPECT_EQ(test, PTR_ERR(target), -EIO);
	ctx->get_inode_loc_err = 0;
}

/* =========================================================================
 * 4. Inline Truncation Tests (ext4_inline_data_truncate)
 * =========================================================================
 */

static void test_inline_data_truncate(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	int has_inline = 1;
	char buf[100];
	int i, ret;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = (char)('a' + (i % 26));

	/* 1. Non-inline inode -> sets *has_inline = 0 and returns */
	inode_lock(inode);
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, has_inline, 0);

	/* 2. Create a 100-byte inline file (60B in i_block + 40B in xattr) */
	ret = create_inline_file(inode, buf, 100);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 100);

	/* 3. Truncate extending i_size (100 -> 120): no-op on inline size */
	inode_lock(inode);
	i_size_write(inode, 120);
	has_inline = 1;
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, has_inline, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 100);

	/* 4. Shrink within xattr space (100 -> 75 bytes) */
	inode_lock(inode);
	i_size_write(inode, 75);
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, has_inline, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, 75);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 75);
	KUNIT_EXPECT_MEMEQ(test, raw_inline_xattr_pos(ctx),
			   buf + EXT4_MIN_INLINE_DATA_SIZE, 15);

	/* 5. Shrink from xattr space into i_block (75 -> 25 bytes): zeros i_block[25..59] */
	inode_lock(inode);
	i_size_write(inode, 25);
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size,
			(u16)EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 25);
	KUNIT_EXPECT_MEMEQ(test, raw_inline_i_block(ctx), buf, 25);
	for (i = 25; i < EXT4_MIN_INLINE_DATA_SIZE; i++)
		KUNIT_EXPECT_EQ(test, ((char *)raw_inline_i_block(ctx))[i], 0);

	/* 6. Shrink to 0 bytes: zeros all of i_block */
	inode_lock(inode);
	i_size_write(inode, 0);
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_disksize, 0);
	for (i = 0; i < EXT4_MIN_INLINE_DATA_SIZE; i++)
		KUNIT_EXPECT_EQ(test, ((char *)raw_inline_i_block(ctx))[i], 0);

	/*
	 * 7. When !EXT4_STATE_MAY_INLINE_DATA (being converted),
	 * ext4_inline_data_truncate clears the extent status tree.
	 */
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	inode_lock(inode);
	ext4_inline_data_truncate(inode, &has_inline);
	inode_unlock(inode);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei.i_orphan));
}

/* =========================================================================
 * 5. Inline-to-Extent Conversion Tests for Regular Files
 * =========================================================================
 */

static void test_convert_inline_data_and_rollback(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	char buf[80];
	int i, ret;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = (char)('A' + (i % 26));

	/* 1. Non-inline inode -> clears MAY_INLINE_DATA and returns 0 immediately */
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA));
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);

	/*
	 * 2. Inline inode without EXT4_STATE_MAY_INLINE_DATA -> calls
	 * filemap_flush() and finishes converting inline data if still inline.
	 */
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_MEMEQ(test, ctx->data_block_buf, buf, sizeof(buf));

	/*
	 * 3. Error rollback when ext4_map_blocks fails (-ENOSPC):
	 * ext4_restore_inline_data restores the original 80 bytes of inline data.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->map_blocks_ret = -ENOSPC;
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, sizeof(buf));
	KUNIT_EXPECT_MEMEQ(test, raw_inline_i_block(ctx), buf,
			   EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_MEMEQ(test, raw_inline_xattr_pos(ctx),
			   buf + EXT4_MIN_INLINE_DATA_SIZE,
			   sizeof(buf) - EXT4_MIN_INLINE_DATA_SIZE);
	ctx->map_blocks_ret = 1;

	/*
	 * 4. Error rollback when ext4_map_blocks returns unmapped block -> -EIO
	 */
	ctx->map_flags = 0;
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));
	ctx->map_flags = EXT4_MAP_MAPPED;

	/*
	 * 5. Error rollback when ext4_inline_getblk returns NULL -> -ENOMEM
	 */
	ctx->getblk_fail = true;
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));
	ctx->getblk_fail = false;

	/*
	 * 6. Error rollback when ext4_handle_dirty_metadata fails on data_bh
	 */
	ctx->dirty_metadata_err = -EIO;
	ctx->dirty_metadata_fail_on_call = 1;
	ctx->dirty_metadata_calls = 0;
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(inode));
	ctx->dirty_metadata_err = 0;
	ctx->dirty_metadata_fail_on_call = 0;

	/*
	 * 7. Successful conversion via ext4_convert_inline_data():
	 * copies 80 bytes into data_bh, destroys inline data, initializes
	 * extent tree.
	 */
	memset(ctx->data_block_buf, 0, sizeof(ctx->data_block_buf));
	ret = ext4_convert_inline_data(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	KUNIT_EXPECT_MEMEQ(test, ctx->data_block_buf, buf, sizeof(buf));
}

static void test_write_triggers_inline_to_extent_conversion(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *inode = &ctx->ei.vfs_inode;
	struct folio *folio = NULL;
	char buf[50];
	int ret;

	memset(buf, 'K', sizeof(buf));

	/*
	 * 1. Standard write (ext4_try_to_write_inline_data) exceeding
	 * TEST_MAX_INLINE_SIZE (pos=0, len=200) converts inline data via
	 * ext4_convert_inline_data_to_extent -> returns 0 so caller proceeds
	 * with normal block write.
	 */
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);

	inode_lock(inode);
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 200,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));

	/*
	 * 2. Conversion with DIOREAD_NOLOCK + DELALLOC in writeback mode
	 * (exercises ext4_should_dioread_nolock -> ext4_get_block_unwritten),
	 * and separately with JOURNAL_DATA mode (exercises
	 * ext4_should_journal_data -> do_journal_get_write_access).
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);

	set_opt(&ctx->sb, DIOREAD_NOLOCK);
	set_opt(&ctx->sb, DELALLOC);
	ctx->journal_mode = EXT4_INODE_WRITEBACK_DATA_MODE;

	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 200,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));

	clear_opt(&ctx->sb, DIOREAD_NOLOCK);
	clear_opt(&ctx->sb, DELALLOC);

	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->journal_mode = EXT4_INODE_JOURNAL_DATA_MODE;

	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 200,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));

	ctx->journal_mode = EXT4_INODE_WRITEBACK_DATA_MODE;

	/*
	 * 3. Error paths in ext4_convert_inline_data_to_extent when
	 * ext4_get_block fails during ext4_block_write_begin (-EIO and -ENOSPC):
	 * cleans up orphan and propagates error.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->map_blocks_ret = -EIO;
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 200,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei.i_orphan));
	ctx->map_blocks_ret = 1;

	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->map_blocks_ret = -ENOSPC;
	inode_lock(inode);
	folio = NULL;
	ret = ext4_try_to_write_inline_data(&inode->i_data, inode, 0, 200,
					    &folio);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(inode));
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->ei.i_orphan));
	ctx->map_blocks_ret = 1;

	/*
	 * 4. Delayed allocation conversion (da = true in
	 * ext4_generic_write_inline_data):
	 *   4a. Fresh inode without inline data yet -> clears MAY_INLINE_DATA
	 *       and returns 0.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	folio = NULL;
	ret = ext4_generic_write_inline_data(&inode->i_data, inode, 0, 200,
					     &folio, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA));

	/*
	 *   4b. Existing inline data -> ext4_da_convert_inline_data_to_extent
	 *       loads inline bytes into folio, marks buffer delayed, and clears
	 *       EXT4_STATE_MAY_INLINE_DATA while keeping inline data until
	 *       writepages.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);
	truncate_inode_pages(&inode->i_data, 0);

	folio = NULL;
	ret = ext4_generic_write_inline_data(&inode->i_data, inode, 0, 200,
					     &folio, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA));

	/*
	 *   4c. Error in ext4_da_get_block_prep during DA conversion ->
	 *       propagates error and unlocks folio.
	 */
	reset_inode_state(ctx, S_IFREG | 0644);
	ret = create_inline_file(inode, buf, sizeof(buf));
	KUNIT_ASSERT_EQ(test, ret, 0);
	truncate_inode_pages(&inode->i_data, 0);

	ctx->da_get_block_prep_err = -ENOSPC;
	inode_lock(inode);
	folio = NULL;
	ret = ext4_generic_write_inline_data(&inode->i_data, inode, 0, 200,
					     &folio, true);
	inode_unlock(inode);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	ctx->da_get_block_prep_err = 0;
}

/* =========================================================================
 * 6. Inline Directory Operations Tests
 * =========================================================================
 */

struct test_dir_ctx {
	struct dir_context ctx;
	int count;
	char names[16][32];
	u64 inos[16];
	int max_emit;
	struct inode *mutate_inode_on_second;
};

static bool test_filldir_actor(struct dir_context *dctx, const char *name,
			       int namelen, loff_t offset, u64 ino,
			       unsigned int d_type)
{
	struct test_dir_ctx *tctx =
		container_of(dctx, struct test_dir_ctx, ctx);

	if (tctx->max_emit > 0 && tctx->count >= tctx->max_emit)
		return false;

	if (tctx->count < 16) {
		int copy_len = min(namelen, 31);

		memcpy(tctx->names[tctx->count], name, copy_len);
		tctx->names[tctx->count][copy_len] = '\0';
		tctx->inos[tctx->count] = ino;
	}
	tctx->count++;

	if (tctx->mutate_inode_on_second && tctx->count == 2) {
		inode_inc_iversion(tctx->mutate_inode_on_second);
		tctx->mutate_inode_on_second = NULL;
	}
	return true;
}

struct inline_dir_test_env {
	struct inode parent;
	struct inode child;
	struct qstr qname;
	struct ext4_filename fname;
	struct dir_private_info info;
	struct file filp;
	struct test_dir_ctx dctx;
	struct ext4_dir_entry_2 bogus_de;
};

static struct inline_dir_test_env *
alloc_dir_test_env(struct kunit *test, struct inline_test_ctx *ctx)
{
	struct inline_dir_test_env *env;

	env = kunit_kzalloc(test, sizeof(*env), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, env);
	env->parent.i_sb = &ctx->sb;
	env->parent.i_ino = TEST_PARENT_INO;
	env->parent.i_mode = S_IFDIR | 0755;
	return env;
}

static void init_mock_fname_and_child(struct inline_dir_test_env *env,
				      struct super_block *sb,
				      const char *name, ino_t ino,
				      umode_t mode)
{
	memset(&env->fname, 0, sizeof(env->fname));
	memset(&env->qname, 0, sizeof(env->qname));
	memset(&env->child, 0, sizeof(env->child));

	env->child.i_sb = sb;
	env->child.i_ino = ino;
	env->child.i_mode = mode;

	env->qname.name = (const unsigned char *)name;
	env->qname.len = strlen(name);
	env->fname.usr_fname = &env->qname;
	env->fname.disk_name.name = (unsigned char *)name;
	env->fname.disk_name.len = strlen(name);
}

static void test_update_final_de(struct kunit *test)
{
	u8 buf[64];
	struct ext4_dir_entry_2 *de1, *de2;

	memset(buf, 0, sizeof(buf));

	/* 1. old_size == 0 -> sets de1->rec_len to new_size (32) */
	de1 = (struct ext4_dir_entry_2 *)buf;
	ext4_update_final_de(buf, 0, 32);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(de1->rec_len), 32);

	/* 2. Two entries in 32 bytes (12 + 20), expand to 56 bytes -> last entry becomes 44 */
	de1->rec_len = cpu_to_le16(12);
	de2 = (struct ext4_dir_entry_2 *)(buf + 12);
	de2->rec_len = cpu_to_le16(20);
	ext4_update_final_de(buf, 32, 56);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(de1->rec_len), 12);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(de2->rec_len), 44);
}

static void test_inline_dir_lifecycle_and_operations(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->ei.vfs_inode;
	struct inline_dir_test_env *env;
	struct ext4_dir_entry_2 *de = NULL;
	struct buffer_head *bh = NULL;
	handle_t *handle;
	int has_inline_data = 1;
	int ret;

	env = alloc_dir_test_env(test, ctx);
	reset_inode_state(ctx, S_IFDIR | 0755);

	/*
	 * 1. Create inline directory via ext4_try_create_inline_dir.
	 */
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_create_inline_dir(handle, &env->parent, dir);
	ext4_journal_stop(handle);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(dir));
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size,
			(u16)EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_EQ(test, i_size_read(dir),
			(loff_t)EXT4_MIN_INLINE_DATA_SIZE);

	/* Newly created inline dir is empty */
	KUNIT_EXPECT_TRUE(test, empty_inline_dir(dir, &has_inline_data));
	KUNIT_EXPECT_EQ(test, has_inline_data, 1);

	/*
	 * 2. Verify ext4_get_first_inline_block returns the inline buffer
	 * starting at parent_de (whose inode field holds the parent ino).
	 */
	bh = ext4_get_first_inline_block(dir, &de, &ret);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_PARENT_INO);
	brelse(bh);

	/*
	 * 3. Add first entry "file1" (fits in i_block alongside parent ino).
	 * Return value 1 means added inline without block conversion.
	 */
	init_mock_fname_and_child(env, &ctx->sb, "file1", 101, S_IFREG | 0644);
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size,
			(u16)EXT4_MIN_INLINE_DATA_SIZE);
	KUNIT_EXPECT_FALSE(test, empty_inline_dir(dir, &has_inline_data));

	/*
	 * 4. Adding duplicate "file1" returns -EEXIST.
	 */
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	/*
	 * 5. Add more entries ("file2_longer_name", "file3_longer_name",
	 * "file4_in_xattr") to overflow i_block (56B) and trigger expansion
	 * into ibody system.data xattr via ext4_update_inline_dir (132B).
	 */
	init_mock_fname_and_child(env, &ctx->sb, "file2_longer_name", 102,
				  S_IFREG | 0644);
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 1);

	init_mock_fname_and_child(env, &ctx->sb, "file3_longer_name", 103,
				  S_IFDIR | 0755);
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, ctx->ei.i_inline_size, TEST_MAX_INLINE_SIZE);

	init_mock_fname_and_child(env, &ctx->sb, "file4_in_xattr", 104,
				  S_IFREG | 0644);
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 1);

	/*
	 * 6. Lookup entries in both i_block ("file1") and xattr ("file4_in_xattr"),
	 * and verify missing entry returns NULL.
	 */
	init_mock_fname_and_child(env, &ctx->sb, "file1", 0, 0);
	bh = ext4_find_inline_entry(dir, &env->fname, &de, &has_inline_data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, has_inline_data, 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), 101U);
	brelse(bh);

	init_mock_fname_and_child(env, &ctx->sb, "file4_in_xattr", 0, 0);
	bh = ext4_find_inline_entry(dir, &env->fname, &de, &has_inline_data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), 104U);
	brelse(bh);

	init_mock_fname_and_child(env, &ctx->sb, "nonexistent", 0, 0);
	bh = ext4_find_inline_entry(dir, &env->fname, &de, &has_inline_data);
	KUNIT_EXPECT_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, has_inline_data, 1);

	/*
	 * 7. Test ext4_read_inline_dir (full iteration, partial iteration,
	 * and iversion change re-validation).
	 */
	env->filp.f_inode = dir;
	env->filp.private_data = &env->info;
	env->dctx.ctx.actor = test_filldir_actor;
	env->dctx.ctx.pos = 0;

	ret = ext4_read_inline_dir(&env->filp, &env->dctx.ctx,
				   &has_inline_data);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, has_inline_data, 1);
	/*
	 * Expect ".", "..", "file1", "file2_longer_name",
	 * "file3_longer_name", "file4_in_xattr"
	 */
	KUNIT_EXPECT_EQ(test, env->dctx.count, 6);
	KUNIT_EXPECT_STREQ(test, env->dctx.names[0], ".");
	KUNIT_EXPECT_STREQ(test, env->dctx.names[1], "..");
	KUNIT_EXPECT_STREQ(test, env->dctx.names[2], "file1");
	KUNIT_EXPECT_STREQ(test, env->dctx.names[5], "file4_in_xattr");
	/*
	 * extra_size = (dotdot_size - EXT4_INLINE_DOTDOT_SIZE) +
	 *              TEST_MAX_INLINE_SIZE = (24 - 4) + 128 = 148
	 */
	KUNIT_EXPECT_EQ(test, env->dctx.ctx.pos,
			(loff_t)(TEST_MAX_INLINE_SIZE + 20));

	/* Calling again at EOF (pos == inline_size) emits nothing */
	env->dctx.count = 0;
	ret = ext4_read_inline_dir(&env->filp, &env->dctx.ctx,
				   &has_inline_data);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, env->dctx.count, 0);

	/*
	 * Mutating inode iversion during iteration and re-entering at nonzero
	 * ctx.pos triggers offset revalidation in ext4_read_inline_dir().
	 */
	memset(&env->dctx, 0, sizeof(env->dctx));
	env->dctx.ctx.actor = test_filldir_actor;
	env->dctx.ctx.pos = 0;
	env->dctx.max_emit = 3;
	env->dctx.mutate_inode_on_second = dir;
	ret = ext4_read_inline_dir(&env->filp, &env->dctx.ctx,
				   &has_inline_data);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, env->dctx.count, 3);
	KUNIT_EXPECT_GT(test, env->dctx.ctx.pos, (loff_t)0);

	env->dctx.max_emit = 0;
	ret = ext4_read_inline_dir(&env->filp, &env->dctx.ctx,
				   &has_inline_data);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, env->dctx.count, 6);

	/*
	 * 8. Test ext4_inlinedir_to_tree (htree rbtree population).
	 */
	{
		struct dir_private_info *info;
		struct dx_hash_info hinfo = {
			.hash_version = DX_HASH_HALF_MD4,
		};

		memset(&env->filp, 0, sizeof(env->filp));
		env->filp.f_inode = dir;
		info = kzalloc_obj(*info, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, info);
		env->filp.private_data = info;
		ret = ext4_inlinedir_to_tree(&env->filp, dir, 0, &hinfo,
					     0, 0, &has_inline_data);
		KUNIT_EXPECT_GT(test, ret, 0);
		KUNIT_EXPECT_EQ(test, has_inline_data, 1);

		/* With start_hash = 0xffffffffU, entries with smaller hash are skipped */
		ext4_htree_free_dir_info(info);
		info = kzalloc_obj(*info, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, info);
		env->filp.private_data = info;
		ret = ext4_inlinedir_to_tree(&env->filp, dir, 0, &hinfo,
					     0xffffffffU, 0xffffffffU,
					     &has_inline_data);
		KUNIT_EXPECT_GE(test, ret, 0);
		ext4_htree_free_dir_info(info);
	}

	/*
	 * 9. Delete entries in both i_block and xattr regions and verify
	 * empty_inline_dir behavior when only xattr has an entry vs completely empty.
	 */
	{
		static const char * const del_names[] = {
			"file1",
			"file2_longer_name",
			"file3_longer_name",
		};
		int i;

		for (i = 0; i < ARRAY_SIZE(del_names); i++) {
			init_mock_fname_and_child(env, &ctx->sb, del_names[i],
						  0, 0);
			bh = ext4_find_inline_entry(dir, &env->fname, &de,
						    &has_inline_data);
			KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
			handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
			KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
			ret = ext4_delete_inline_entry(handle, dir, de, bh,
						       &has_inline_data);
			ext4_journal_stop(handle);
			KUNIT_EXPECT_EQ(test, ret, 0);
			brelse(bh);
		}
	}

	/* i_block now has no live entries, but "file4_in_xattr" is still in xattr */
	KUNIT_EXPECT_FALSE(test, empty_inline_dir(dir, &has_inline_data));

	/* Delete "file4_in_xattr" -> directory becomes completely empty */
	init_mock_fname_and_child(env, &ctx->sb, "file4_in_xattr", 0, 0);
	bh = ext4_find_inline_entry(dir, &env->fname, &de, &has_inline_data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);

	/* Deleting an address outside inline buffers returns -ENOENT */
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_delete_inline_entry(handle, dir, &env->bogus_de, bh,
				       &has_inline_data);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_delete_inline_entry(handle, dir, de, bh, &has_inline_data);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 0);
	brelse(bh);

	KUNIT_EXPECT_TRUE(test, empty_inline_dir(dir, &has_inline_data));
}

static void test_inline_dir_conversion_and_corruptions(struct kunit *test)
{
	struct inline_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->ei.vfs_inode;
	struct inline_dir_test_env *env;
	struct ext4_dir_entry_2 *de;
	handle_t *handle;
	int has_inline_data = 1;
	int i, ret;

	env = alloc_dir_test_env(test, ctx);
	reset_inode_state(ctx, S_IFDIR | 0755);

	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_create_inline_dir(handle, &env->parent, dir);
	ext4_journal_stop(handle);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Fill the inline directory up to its 128-byte limit */
	for (i = 0; i < 4; i++) {
		char name[24];

		snprintf(name, sizeof(name), "entry_long_name_%d", i);
		init_mock_fname_and_child(env, &ctx->sb, name, 200 + i,
					  S_IFREG | 0644);
		handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
		KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
		ret = ext4_try_add_inline_entry(handle, &env->fname, dir,
						&env->child);
		ext4_journal_stop(handle);
		KUNIT_ASSERT_EQ(test, ret, 1);
	}

	/*
	 * 1. When full, adding another entry triggers ext4_convert_inline_data_nolock.
	 * Test rollback first when ext4_map_blocks fails (-ENOSPC).
	 */
	init_mock_fname_and_child(env, &ctx->sb, "overflow_entry_name", 250,
				  S_IFREG | 0644);
	ctx->map_blocks_ret = -ENOSPC;
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(dir));
	ctx->map_blocks_ret = 1;

	/*
	 * 2. Successful conversion of full inline directory to a normal
	 * 1024-byte directory block -> ext4_try_add_inline_entry returns 0
	 * and data_block_buf starts with "." and ".." followed by copied entries.
	 */
	memset(ctx->data_block_buf, 0, sizeof(ctx->data_block_buf));
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_add_inline_entry(handle, &env->fname, dir, &env->child);
	ext4_journal_stop(handle);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ext4_has_inline_data(dir));
	KUNIT_EXPECT_EQ(test, i_size_read(dir), (loff_t)TEST_BLOCK_SIZE);

	/* Verify "." and ".." headers in converted block */
	de = (struct ext4_dir_entry_2 *)ctx->data_block_buf;
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_INO);
	KUNIT_EXPECT_EQ(test, de->name_len, 1);
	KUNIT_EXPECT_EQ(test, de->name[0], '.');
	de = ext4_next_entry(de, TEST_BLOCK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_PARENT_INO);
	KUNIT_EXPECT_EQ(test, de->name_len, 2);
	KUNIT_EXPECT_MEMEQ(test, de->name, "..", 2);

	/*
	 * 3. Non-inline directory behavior on inline dir functions:
	 * sets *has_inline_data = 0 and returns early.
	 */
	has_inline_data = 1;
	KUNIT_EXPECT_TRUE(test, empty_inline_dir(dir, &has_inline_data));
	KUNIT_EXPECT_EQ(test, has_inline_data, 0);

	/*
	 * 4. Corrupted inline directory checks:
	 *   4a. Parent ino ("..") == 0 -> empty_inline_dir returns false.
	 *   4b. Zero rec_len dirent -> empty_inline_dir returns false and
	 *       ext4_convert_inline_data aborts via ext4_check_all_de.
	 */
	reset_inode_state(ctx, S_IFDIR | 0755);
	handle = ext4_journal_start(dir, EXT4_HT_DIR, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(handle));
	ret = ext4_try_create_inline_dir(handle, &env->parent, dir);
	ext4_journal_stop(handle);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Corrupt parent ino to 0 */
	*(__le32 *)raw_inline_i_block(ctx) = cpu_to_le32(0);
	KUNIT_EXPECT_FALSE(test, empty_inline_dir(dir, &has_inline_data));
	*(__le32 *)raw_inline_i_block(ctx) = cpu_to_le32(TEST_PARENT_INO);

	/* Corrupt first dirent rec_len to 0 */
	de = (struct ext4_dir_entry_2 *)((char *)raw_inline_i_block(ctx) +
					 EXT4_INLINE_DOTDOT_SIZE);
	de->rec_len = cpu_to_le16(0);
	KUNIT_EXPECT_FALSE(test, empty_inline_dir(dir, &has_inline_data));

	/* ext4_convert_inline_data on corrupted inline dir fails in ext4_check_all_de */
	ret = ext4_convert_inline_data(dir);
	KUNIT_EXPECT_EQ(test, ret, -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(dir));
}

static struct kunit_case ext4_inline_test_cases[] = {
	KUNIT_CASE(test_max_inline_size_variants),
	KUNIT_CASE(test_find_inline_data_nolock),
	KUNIT_CASE(test_write_and_destroy_inline_data),
	KUNIT_CASE(test_write_inline_short_copy_and_edge_cases),
	KUNIT_CASE(test_readpage_inline_link_and_iomap),
	KUNIT_CASE(test_inline_data_truncate),
	KUNIT_CASE(test_convert_inline_data_and_rollback),
	KUNIT_CASE(test_write_triggers_inline_to_extent_conversion),
	KUNIT_CASE(test_update_final_de),
	KUNIT_CASE(test_inline_dir_lifecycle_and_operations),
	KUNIT_CASE(test_inline_dir_conversion_and_corruptions),
	{}
};

static struct kunit_suite ext4_inline_test_suite = {
	.name = "ext4_inline_test",
	.init = inline_test_init,
	.exit = inline_test_exit,
	.test_cases = ext4_inline_test_cases,
};

kunit_test_suites(&ext4_inline_test_suite);

MODULE_LICENSE("GPL");
