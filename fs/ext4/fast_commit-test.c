// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 fast commit operations (fs/ext4/fast_commit.c)
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "extents_status.h"
#include "fast_commit.h"
#include "namei.h"

#define TEST_BLK_SIZE		1024
#define TEST_BLK_BITS		10
#define TEST_INODE_SIZE		256
#define TEST_EXTRA_ISIZE	32
#define TEST_FIRST_PBLK		100
#define TEST_MAX_BLOCKS		32
#define TEST_MAX_LBLOCKS	16
#define TEST_MAX_INODES		8
#define TEST_MAX_FC_BUFS	16
#define TEST_MAX_BB_MARKS	32

#define TEST_ROOT_INO		EXT4_ROOT_INO
#define TEST_DIR_INO		12
#define TEST_FILE1_INO		13
#define TEST_FILE2_INO		14
#define TEST_SUBDIR_INO		15
#define TEST_CSUM_SEED		0x12345678U

struct fc_mock_block {
	bool in_use;
	struct buffer_head bh;
	char data[TEST_BLK_SIZE] __aligned(8);
};

struct fc_mock_inode_slot {
	bool allocated;
	struct ext4_inode_info ei;
	struct jbd2_inode jinode;
	struct buffer_head iloc_bh;
	char raw_buf[TEST_BLK_SIZE] __aligned(8);
	ext4_fsblk_t lblk_to_pblk[TEST_MAX_LBLOCKS];
};

struct fc_bb_mark {
	ext4_fsblk_t block;
	int len;
	bool state;
};

struct fc_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct ext4_super_block es;
	struct block_device bdev;
	struct block_device ext_jdev;
	struct address_space bd_mapping;
	struct buffer_head sb_bh;

	journal_t journal;
	transaction_t running_txn;
	handle_t handle;
	struct buffer_head *fc_wbuf[TEST_MAX_FC_BUFS];
	struct buffer_head fc_bhs[TEST_MAX_FC_BUFS];
	char fc_buf_data[TEST_MAX_FC_BUFS][TEST_BLK_SIZE] __aligned(8);

	struct fc_mock_inode_slot inodes[TEST_MAX_INODES];
	struct fc_mock_block blocks[TEST_MAX_BLOCKS];

	/* Shrinker callbacks saved during init */
	unsigned long (*shrinker_count)(struct shrinker *shrink,
					struct shrink_control *sc);
	unsigned long (*shrinker_scan)(struct shrinker *shrink,
				       struct shrink_control *sc);
	bool es_shrinker_inited;

	/* Fault injection and call tracking */
	int journal_mode;
	int fail_get_inode_loc;
	int fail_get_inode_loc_noio;
	int fail_get_fc_inode_loc;
	int fail_dirty_metadata;
	int fail_dirty_metadata_on_call;
	int dirty_metadata_calls;
	bool fail_sync_dirty_buffer;
	int fail_mark_inode_used;
	int fail_iget;
	int fail_iget_on_call;
	unsigned long fail_iget_ino;
	int iget_calls;
	int fail_map_blocks;
	int force_map_blocks_ret;
	ext4_fsblk_t force_map_pblk;
	unsigned int force_map_flags;
	int fail_find_extent;
	int fail_insert_extent;
	int fail_fc_get_buf;
	int fail_fc_get_buf_on_call;
	int fc_get_buf_calls;
	bool fail_fc_wait_bufs;
	int submit_inode_data_err;

	unsigned int fc_submit_bh_calls;
	unsigned int fc_submit_tail_calls;
	unsigned int fc_flush_calls;
	struct block_device *last_flushed_bdev;
	unsigned int mark_inode_used_calls;
	int last_marked_ino;

	struct fc_bb_mark bb_marks[TEST_MAX_BB_MARKS];
	unsigned int nr_bb_marks;

	/* Custom ES lookup stub override */
	bool use_custom_es_lookup;
	struct extent_status custom_es;
	int custom_es_ret;
};

#ifdef CONFIG_QUOTA
static struct dquot __rcu **fc_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *fc_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations fc_test_qops = {
	.get_reserved_space = fc_test_get_reserved_space,
};
#endif

static const struct super_operations fc_test_sops = {
#ifdef CONFIG_QUOTA
	.get_dquots	= fc_test_get_dquots,
#endif
};

static unsigned long fc_dummy_shrinker_count(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	return 0;
}

static unsigned long fc_dummy_shrinker_scan(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	return SHRINK_STOP;
}

static int fc_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static int fc_set_anon(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static struct file_system_type fc_fs_type = {
	.name		 = "fast_commit_test",
	.init_fs_context = fc_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

/* --- Mock Inode, Block, and Dentry Helpers --- */

static struct fc_mock_inode_slot *find_slot_by_ino(struct fc_test_ctx *ctx,
						   unsigned long ino)
{
	int i;

	for (i = 0; i < TEST_MAX_INODES; i++) {
		if (ctx->inodes[i].allocated &&
		    ctx->inodes[i].ei.vfs_inode.i_ino == ino)
			return &ctx->inodes[i];
	}
	return NULL;
}

static struct fc_mock_inode_slot *find_slot(struct fc_test_ctx *ctx,
					    struct inode *inode)
{
	return find_slot_by_ino(ctx, inode->i_ino);
}

static struct inode *get_mock_inode(struct fc_test_ctx *ctx, unsigned long ino)
{
	struct fc_mock_inode_slot *slot = find_slot_by_ino(ctx, ino);

	return slot ? &slot->ei.vfs_inode : NULL;
}

static struct fc_mock_block *get_mock_block(struct fc_test_ctx *ctx,
					    ext4_fsblk_t pblk)
{
	unsigned long idx;

	if (pblk < TEST_FIRST_PBLK)
		return NULL;
	idx = pblk - TEST_FIRST_PBLK;
	if (idx >= TEST_MAX_BLOCKS)
		return NULL;
	return &ctx->blocks[idx];
}

static ext4_fsblk_t alloc_mock_pblk(struct fc_test_ctx *ctx)
{
	int i;

	for (i = 0; i < TEST_MAX_BLOCKS; i++) {
		if (!ctx->blocks[i].in_use) {
			ctx->blocks[i].in_use = true;
			memset(ctx->blocks[i].data, 0, TEST_BLK_SIZE);
			set_buffer_uptodate(&ctx->blocks[i].bh);
			clear_buffer_verified(&ctx->blocks[i].bh);
			return TEST_FIRST_PBLK + i;
		}
	}
	return 0;
}

static void init_mock_inode_slot(struct fc_test_ctx *ctx,
				 struct fc_mock_inode_slot *slot,
				 unsigned long ino, umode_t mode)
{
	struct inode *inode = &slot->ei.vfs_inode;
	struct ext4_inode *raw_inode;

	memset(slot, 0, sizeof(*slot));
	slot->allocated = true;

	inode->i_sb = ctx->sb;
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = TEST_BLK_BITS;
	inode->i_flags |= S_NOQUOTA;
	inode->i_mapping = &inode->i_data;
	set_nlink(inode, S_ISDIR(mode) ? 2 : 1);
	atomic_set(&inode->i_count, 1);

	spin_lock_init(&inode->i_lock);
	i_size_ordered_init(inode);
	init_rwsem(&inode->i_rwsem);
	init_rwsem(&inode->i_data.invalidate_lock);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	INIT_HLIST_NODE(&inode->i_hash);
	inode_state_set_raw(inode, I_DIRTY);

	init_rwsem(&slot->ei.xattr_sem);
	init_rwsem(&slot->ei.i_data_sem);
	INIT_LIST_HEAD(&slot->ei.i_orphan);
	spin_lock_init(&slot->ei.i_fc_lock);
	spin_lock_init(&slot->ei.i_raw_lock);
	spin_lock_init(&slot->ei.i_block_reservation_lock);
	ext4_es_init_tree(&slot->ei.i_es_tree);
	ext4_init_pending_tree(&slot->ei.i_pending_tree);
	rwlock_init(&slot->ei.i_es_lock);
	INIT_LIST_HEAD(&slot->ei.i_es_list);
	ext4_fc_init_inode(inode);

	slot->ei.i_extra_isize = TEST_EXTRA_ISIZE;
	slot->ei.i_csum_seed = TEST_CSUM_SEED ^ (u32)ino;
	slot->ei.i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	jbd2_journal_init_jbd_inode(&slot->jinode, inode);
	slot->ei.jinode = &slot->jinode;

	slot->iloc_bh.b_data = slot->raw_buf;
	slot->iloc_bh.b_size = TEST_BLK_SIZE;
	slot->iloc_bh.b_blocknr = 10 + ino;
	slot->iloc_bh.b_bdev = &ctx->bdev;
	atomic_set(&slot->iloc_bh.b_count, 1);
	set_buffer_uptodate(&slot->iloc_bh);
	set_buffer_mapped(&slot->iloc_bh);

	raw_inode = (struct ext4_inode *)slot->raw_buf;
	raw_inode->i_mode = cpu_to_le16(mode);
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	raw_inode->i_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);
	if (S_ISREG(mode)) {
		ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
		ext4_ext_tree_init(NULL, inode);
		raw_inode->i_flags = cpu_to_le32(EXT4_EXTENTS_FL);
		memcpy(raw_inode->i_block, slot->ei.i_data,
		       sizeof(raw_inode->i_block));
	}
}

static void init_mock_dentry(struct dentry *dentry, struct dentry *parent,
			     const char *name, struct inode *inode,
			     struct super_block *sb)
{
	memset(dentry, 0, sizeof(*dentry));
	spin_lock_init(&dentry->d_lock);
	seqcount_spinlock_init(&dentry->d_seq, &dentry->d_lock);
	INIT_HLIST_NODE(&dentry->d_sib);
	INIT_HLIST_HEAD(&dentry->d_children);
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	dentry->d_parent = parent ? parent : dentry;
	dentry->d_sb = sb;
	strscpy((char *)dentry->d_shortname.string, name,
		sizeof(dentry->d_shortname.string));
	dentry->__d_name.name = dentry->d_shortname.string;
	dentry->__d_name.len = strlen(name);
	if (inode) {
		dentry->d_inode = inode;
		dentry->d_flags = S_ISDIR(inode->i_mode) ?
				  DCACHE_DIRECTORY_TYPE : DCACHE_REGULAR_TYPE;
	} else {
		dentry->d_flags = DCACHE_MISS_TYPE;
	}
}

static void sync_inode_to_raw(struct fc_mock_inode_slot *slot)
{
	struct inode *inode = &slot->ei.vfs_inode;
	struct ext4_inode *raw = (struct ext4_inode *)slot->raw_buf;

	raw->i_mode = cpu_to_le16(inode->i_mode);
	raw->i_links_count = cpu_to_le16(inode->i_nlink);
	raw->i_size_lo = cpu_to_le32(lower_32_bits(inode->i_size));
	raw->i_size_high = cpu_to_le32(upper_32_bits(inode->i_size));
	raw->i_flags = cpu_to_le32(slot->ei.i_flags);
	raw->i_generation = cpu_to_le32(inode->i_generation);
	raw->i_extra_isize = cpu_to_le16(slot->ei.i_extra_isize);
	memcpy(raw->i_block, slot->ei.i_data, sizeof(raw->i_block));
}

/* --- KUnit Static Stubs --- */

static int stub_inode_journal_mode(struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;

	return ctx->journal_mode;
}

static void stub_da_update_reserve_space(struct inode *inode, int used,
					 int quota_claim)
{
}

static void stub_da_release_space(struct inode *inode, int to_free)
{
}

static int stub_get_inode_loc(struct inode *inode, struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;

	if (ctx->fail_get_inode_loc)
		return ctx->fail_get_inode_loc;

	slot = find_slot(ctx, inode);
	if (!slot)
		return -ENOENT;

	get_bh(&slot->iloc_bh);
	iloc->bh = &slot->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int stub_get_inode_loc_noio(struct inode *inode, struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;

	if (ctx->fail_get_inode_loc_noio)
		return ctx->fail_get_inode_loc_noio;

	slot = find_slot(ctx, inode);
	if (!slot)
		return -EAGAIN;

	get_bh(&slot->iloc_bh);
	iloc->bh = &slot->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int stub_get_fc_inode_loc(struct super_block *sb, unsigned long ino,
				 struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;

	if (ctx->fail_get_fc_inode_loc)
		return ctx->fail_get_fc_inode_loc;

	slot = find_slot_by_ino(ctx, ino);
	if (!slot)
		return -ENOENT;

	get_bh(&slot->iloc_bh);
	iloc->bh = &slot->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = 0;
	return 0;
}

static int stub_handle_dirty_metadata(const char *where, unsigned int line,
				      handle_t *handle, struct inode *inode,
				      struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;

	ctx->dirty_metadata_calls++;
	if (ctx->fail_dirty_metadata_on_call > 0 &&
	    ctx->dirty_metadata_calls == ctx->fail_dirty_metadata_on_call)
		return ctx->fail_dirty_metadata;
	if (ctx->fail_dirty_metadata_on_call == 0 && ctx->fail_dirty_metadata)
		return ctx->fail_dirty_metadata;

	if (ctx->fail_sync_dirty_buffer) {
		/*
		 * __sync_dirty_buffer() checks test_clear_buffer_dirty(bh)
		 * and returns -EIO without submitting a bio if !buffer_mapped.
		 */
		set_buffer_dirty(bh);
		clear_buffer_mapped(bh);
	} else {
		clear_buffer_dirty(bh);
		set_buffer_mapped(bh);
	}
	return 0;
}

static int stub_mark_iloc_dirty(handle_t *handle, struct inode *inode,
				struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot = find_slot(ctx, inode);

	if (slot)
		sync_inode_to_raw(slot);
	brelse(iloc->bh);
	return 0;
}

static int stub_mark_inode_dirty(handle_t *handle, struct inode *inode,
				 const char *func, unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot = find_slot(ctx, inode);

	if (slot)
		sync_inode_to_raw(slot);
	return 0;
}

static int stub_ext_dirty(const char *where, unsigned int line,
			  handle_t *handle, struct inode *inode,
			  struct ext4_ext_path *path)
{
	return 0;
}

static void stub_free_blocks(handle_t *handle, struct inode *inode,
			     struct buffer_head *bh, ext4_fsblk_t block,
			     unsigned long count, int flags)
{
}

static int stub_orphan_add(handle_t *handle, struct inode *inode)
{
	return 0;
}

static int stub_orphan_del(handle_t *handle, struct inode *inode)
{
	return 0;
}

static int stub_mark_inode_used(struct super_block *sb, int ino)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;

	ctx->mark_inode_used_calls++;
	ctx->last_marked_ino = ino;
	if (ctx->fail_mark_inode_used)
		return ctx->fail_mark_inode_used;
	return 0;
}

static void stub_mb_mark_bb(struct super_block *sb, ext4_fsblk_t block,
			    int len, bool state)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;

	if (ctx->nr_bb_marks < TEST_MAX_BB_MARKS) {
		ctx->bb_marks[ctx->nr_bb_marks].block = block;
		ctx->bb_marks[ctx->nr_bb_marks].len = len;
		ctx->bb_marks[ctx->nr_bb_marks].state = state;
	}
	ctx->nr_bb_marks++;
}

static struct inode *stub_ext4_iget(struct super_block *sb, unsigned long ino,
				    ext4_iget_flags flags,
				    const char *function, unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;
	struct inode *inode;
	struct ext4_inode *raw;

	ctx->iget_calls++;
	if (ctx->fail_iget_on_call > 0 &&
	    ctx->iget_calls == ctx->fail_iget_on_call)
		return ERR_PTR(ctx->fail_iget ? ctx->fail_iget : -ENOENT);
	if (ctx->fail_iget_ino && ino == ctx->fail_iget_ino)
		return ERR_PTR(ctx->fail_iget ? ctx->fail_iget : -ENOENT);
	if (ctx->fail_iget && !ctx->fail_iget_on_call && !ctx->fail_iget_ino)
		return ERR_PTR(ctx->fail_iget);

	slot = find_slot_by_ino(ctx, ino);
	if (!slot)
		return ERR_PTR(-ENOENT);

	inode = &slot->ei.vfs_inode;
	raw = (struct ext4_inode *)slot->raw_buf;
	if (raw->i_mode) {
		inode->i_mode = le16_to_cpu(raw->i_mode);
		set_nlink(inode, le16_to_cpu(raw->i_links_count));
		inode->i_generation = le32_to_cpu(raw->i_generation);
		i_size_write(inode, ext4_isize(sb, raw));
		slot->ei.i_flags = le32_to_cpu(raw->i_flags);
		if (slot->ei.i_flags & EXT4_INLINE_DATA_FL) {
			ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
			memcpy(slot->ei.i_data, raw->i_block,
			       sizeof(raw->i_block));
		} else if (slot->ei.i_flags & EXT4_EXTENTS_FL) {
			struct ext4_extent_header *eh =
				(struct ext4_extent_header *)raw->i_block;
			struct ext4_extent_header *ieh =
				ext_inode_hdr(&slot->ei.vfs_inode);

			if (eh->eh_magic == EXT4_EXT_MAGIC &&
			    ieh->eh_entries == 0)
				memcpy(slot->ei.i_data, raw->i_block,
				       sizeof(raw->i_block));
		}
	}

	atomic_inc(&inode->i_count);
	return inode;
}

static int stub_map_blocks(handle_t *handle, struct inode *inode,
			   struct ext4_map_blocks *map, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;
	ext4_fsblk_t pblk;

	if (ctx->fail_map_blocks)
		return ctx->fail_map_blocks;
	if (ctx->force_map_blocks_ret) {
		map->m_pblk = ctx->force_map_pblk;
		map->m_flags = ctx->force_map_flags;
		if (ctx->force_map_blocks_ret > 0)
			map->m_len = ctx->force_map_blocks_ret;
		return ctx->force_map_blocks_ret;
	}

	if (S_ISREG(inode->i_mode) &&
	    ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		kunit_deactivate_static_stub(test, ext4_map_blocks);
		int ret = ext4_ext_map_blocks(handle, inode, map, flags);

		kunit_activate_static_stub(test, ext4_map_blocks,
					   stub_map_blocks);
		return ret;
	}

	slot = find_slot(ctx, inode);
	if (!slot || map->m_lblk >= TEST_MAX_LBLOCKS)
		return -EFSCORRUPTED;

	pblk = slot->lblk_to_pblk[map->m_lblk];
	if (pblk) {
		map->m_pblk = pblk;
		map->m_len = 1;
		map->m_flags = EXT4_MAP_MAPPED;
		return 1;
	}

	if (!(flags & EXT4_GET_BLOCKS_CREATE)) {
		map->m_pblk = 0;
		map->m_flags = 0;
		return 0;
	}

	pblk = alloc_mock_pblk(ctx);
	if (!pblk)
		return -ENOSPC;

	slot->lblk_to_pblk[map->m_lblk] = pblk;
	map->m_pblk = pblk;
	map->m_len = 1;
	map->m_flags = EXT4_MAP_MAPPED | EXT4_MAP_NEW;
	return 1;
}

static struct buffer_head *stub_ext4_getblk(handle_t *handle,
					    struct inode *inode,
					    ext4_lblk_t block, int map_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct fc_mock_inode_slot *slot;
	struct fc_mock_block *blk;
	ext4_fsblk_t pblk;

	slot = find_slot(ctx, inode);
	if (!slot || block >= TEST_MAX_LBLOCKS)
		return ERR_PTR(-EFSCORRUPTED);

	pblk = slot->lblk_to_pblk[block];
	if (!pblk) {
		if (!(map_flags & EXT4_GET_BLOCKS_CREATE))
			return NULL;
		pblk = alloc_mock_pblk(ctx);
		if (!pblk)
			return ERR_PTR(-ENOSPC);
		slot->lblk_to_pblk[block] = pblk;
	}

	blk = get_mock_block(ctx, pblk);
	if (!blk)
		return ERR_PTR(-EFSCORRUPTED);

	get_bh(&blk->bh);
	return &blk->bh;
}

static struct ext4_ext_path *stub_find_extent(struct inode *inode,
					      ext4_lblk_t block,
					      struct ext4_ext_path *path,
					      int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct ext4_ext_path *ret;

	if (ctx->fail_find_extent)
		return ERR_PTR(ctx->fail_find_extent);

	kunit_deactivate_static_stub(test, ext4_find_extent);
	ret = ext4_find_extent(inode, block, path, flags);
	kunit_activate_static_stub(test, ext4_find_extent, stub_find_extent);
	return ret;
}

static struct ext4_ext_path *stub_insert_extent(handle_t *handle,
						struct inode *inode,
						struct ext4_ext_path *path,
						struct ext4_extent *newext,
						int gb_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct ext4_ext_path *ret;

	if (ctx->fail_insert_extent) {
		ext4_free_ext_path(path);
		return ERR_PTR(ctx->fail_insert_extent);
	}

	kunit_deactivate_static_stub(test, ext4_ext_insert_extent);
	ret = ext4_ext_insert_extent(handle, inode, path, newext, gb_flags);
	kunit_activate_static_stub(test, ext4_ext_insert_extent,
				   stub_insert_extent);
	return ret;
}

static int stub_es_lookup_extent(struct inode *inode, ext4_lblk_t lblk,
				 ext4_lblk_t *next_lblk,
				 struct extent_status *es, u64 *pseq)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	int ret;

	if (ctx->use_custom_es_lookup) {
		*es = ctx->custom_es;
		return ctx->custom_es_ret;
	}

	kunit_deactivate_static_stub(test, ext4_es_lookup_extent);
	ret = ext4_es_lookup_extent(inode, lblk, next_lblk, es, pseq);
	kunit_activate_static_stub(test, ext4_es_lookup_extent,
				   stub_es_lookup_extent);
	return ret;
}

static int stub_fc_get_buf(journal_t *journal, struct buffer_head **bh_out)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct buffer_head *bh;
	int fc_off;

	*bh_out = NULL;
	ctx->fc_get_buf_calls++;
	if (ctx->fail_fc_get_buf_on_call > 0 &&
	    ctx->fc_get_buf_calls == ctx->fail_fc_get_buf_on_call)
		return ctx->fail_fc_get_buf ? ctx->fail_fc_get_buf : -ENOSPC;
	if (ctx->fail_fc_get_buf && !ctx->fail_fc_get_buf_on_call)
		return ctx->fail_fc_get_buf;

	if (journal->j_fc_off + journal->j_fc_first >= journal->j_fc_last)
		return -EINVAL;

	fc_off = journal->j_fc_off;
	if (fc_off >= TEST_MAX_FC_BUFS)
		return -ENOSPC;

	journal->j_fc_off++;
	bh = &ctx->fc_bhs[fc_off];
	memset(bh->b_data, 0, journal->j_blocksize);
	set_buffer_uptodate(bh);
	clear_buffer_write_io_error(bh);
	get_bh(bh);
	journal->j_fc_wbuf[fc_off] = bh;
	*bh_out = bh;
	return 0;
}

static void stub_fc_submit_bh(struct super_block *sb, bool is_tail)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;
	struct buffer_head *bh = EXT4_SB(sb)->s_fc_bh;

	ctx->fc_submit_bh_calls++;
	if (is_tail)
		ctx->fc_submit_tail_calls++;
	set_buffer_dirty(bh);
	if (ctx->fail_fc_wait_bufs) {
		clear_buffer_uptodate(bh);
		set_buffer_write_io_error(bh);
	} else {
		set_buffer_uptodate(bh);
		clear_buffer_write_io_error(bh);
	}
	EXT4_SB(sb)->s_fc_bh = NULL;
}

static int stub_fc_issue_flush(struct block_device *bdev)
{
	struct kunit *test = kunit_get_current_test();
	struct fc_test_ctx *ctx = test->priv;

	ctx->fc_flush_calls++;
	ctx->last_flushed_bdev = bdev;
	return 0;
}

static int mock_submit_inode_data_buffers(struct jbd2_inode *jinode)
{
	struct inode *inode = jinode->i_vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct fc_test_ctx *ctx = container_of(sbi->s_es, struct fc_test_ctx, es);

	return ctx->submit_inode_data_err;
}

/*
 * Wrapper for journal->j_fc_cleanup_callback used during ext4_fc_commit() tests.
 * Calls the real ext4_fc_cleanup() and, on fallback commits (tid != 0), marks
 * the mock transaction as committed so jbd2_complete_transaction() returns
 * immediately without waiting for a kjournald2 thread.
 */
static void test_j_fc_cleanup_cb(journal_t *journal, int full, tid_t tid)
{
	ext4_fc_cleanup_test(journal, full, tid);
	if (!full && tid != 0) {
		write_lock(&journal->j_state_lock);
		journal->j_commit_request = tid;
		journal->j_commit_sequence = tid;
		journal->j_flags &= ~JBD2_FULL_COMMIT_ONGOING;
		write_unlock(&journal->j_state_lock);
	}
}

/* --- Suite Setup & Teardown --- */

static void fc_cleanup_all(void *priv)
{
	struct fc_test_ctx *ctx = priv;
	int i;

	ctx->sbi->s_mount_state &= ~EXT4_FC_REPLAY;
	if (ctx->journal.j_private) {
		/*
		 * Run cleanup twice: the first pass frees FC_Q_MAIN and
		 * splices FC_Q_STAGING onto FC_Q_MAIN; the second frees the
		 * spliced staging entries.
		 */
		ext4_fc_cleanup_test(&ctx->journal, 1, 999999);
		ext4_fc_cleanup_test(&ctx->journal, 1, 999999);
	}
	ext4_fc_replay_cleanup(ctx->sb);

	for (i = 0; i < TEST_MAX_INODES; i++) {
		if (!ctx->inodes[i].allocated)
			continue;
		down_write(&ctx->inodes[i].ei.i_data_sem);
		ext4_es_remove_extent(&ctx->inodes[i].ei.vfs_inode, 0,
				      EXT_MAX_BLOCKS);
		up_write(&ctx->inodes[i].ei.i_data_sem);
		ext4_fc_free_inode_snap_test(&ctx->inodes[i].ei.vfs_inode);
	}

	if (ctx->es_shrinker_inited) {
		ext4_es_unregister_shrinker(ctx->sbi);
		ext4_es_destroy_stats(ctx->sbi);
	}
	deactivate_super(ctx->sb);
}

static int fast_commit_test_init(struct kunit *test)
{
	static struct lock_class_key fc_test_commit_key;
	struct fc_test_ctx *ctx;
	struct ext4_sb_info *sbi;
	struct super_block *sb;
	struct fs_context *fc;
	int i, err;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sbi);

	fc = fs_context_for_mount(&fc_fs_type, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(fc));
	sb = sget_fc(fc, NULL, fc_set_anon);
	put_fs_context(fc);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb));

	ctx->sb = sb;
	ctx->sbi = sbi;
	test->priv = ctx;

	/* Superblock and ext4_sb_info setup */
	sb->s_blocksize = TEST_BLK_SIZE;
	sb->s_blocksize_bits = TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_bdi = &noop_backing_dev_info;
	sb->s_fs_info = sbi;
	sb->s_op = &fc_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &fc_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	strscpy(sb->s_id, "ext4-fc-test", sizeof(sb->s_id));

	ctx->bdev.bd_mapping = &ctx->bd_mapping;
	sb->s_bdev = &ctx->bdev;

	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_first_ino = cpu_to_le32(EXT4_GOOD_OLD_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(TEST_INODE_SIZE);
	ctx->es.s_inodes_count = cpu_to_le32(1024);
	ctx->es.s_first_data_block = cpu_to_le32(1);
	ctx->es.s_feature_compat =
		cpu_to_le32(EXT4_FEATURE_COMPAT_DIR_INDEX |
			    EXT4_FEATURE_COMPAT_FAST_COMMIT);
	ctx->es.s_feature_incompat =
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_FILETYPE |
			    EXT4_FEATURE_INCOMPAT_EXTENTS |
			    EXT4_FEATURE_INCOMPAT_INLINE_DATA);
	ctx->es.s_feature_ro_compat =
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);

	sbi->s_sb = sb;
	sbi->s_es = &ctx->es;
	sbi->s_sbh = &ctx->sb_bh;
	sbi->s_first_ino = EXT4_GOOD_OLD_FIRST_INO;
	sbi->s_inode_size = TEST_INODE_SIZE;
	sbi->s_want_extra_isize = TEST_EXTRA_ISIZE;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE;
	sbi->s_blocks_per_group = 32768;
	sbi->s_inodes_per_group = 1024;
	sbi->s_inodes_per_block = TEST_BLK_SIZE / TEST_INODE_SIZE;
	sbi->s_csum_seed = TEST_CSUM_SEED;
	sbi->s_cluster_bits = 0;
	sbi->s_cluster_ratio = 1;
	set_opt(sb, ERRORS_CONT);
	set_opt2(sb, JOURNAL_FAST_COMMIT);

	mutex_init(&sbi->s_fc_lock);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_STAGING]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_STAGING]);
	atomic_set(&sbi->s_fc_subtid, 0);
	INIT_LIST_HEAD(&sbi->s_orphan);
	mutex_init(&sbi->s_orphan_lock);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	up_write(&sb->s_umount);

	err = kunit_add_action_or_reset(test, fc_cleanup_all, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = ext4_es_register_shrinker(sbi);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->es_shrinker_inited = true;
	ctx->shrinker_count = sbi->s_es_shrinker->count_objects;
	ctx->shrinker_scan = sbi->s_es_shrinker->scan_objects;
	WRITE_ONCE(sbi->s_es_shrinker->count_objects, fc_dummy_shrinker_count);
	WRITE_ONCE(sbi->s_es_shrinker->scan_objects, fc_dummy_shrinker_scan);

	/* Mock journal, running transaction, and handle setup */
	rwlock_init(&ctx->journal.j_state_lock);
	spin_lock_init(&ctx->journal.j_list_lock);
	mutex_init(&ctx->journal.j_barrier);
	lockdep_init_map(&ctx->journal.j_trans_commit_map, "jbd2_handle",
			 &fc_test_commit_key, 0);
	init_waitqueue_head(&ctx->journal.j_wait_transaction_locked);
	init_waitqueue_head(&ctx->journal.j_wait_done_commit);
	init_waitqueue_head(&ctx->journal.j_wait_commit);
	init_waitqueue_head(&ctx->journal.j_wait_updates);
	init_waitqueue_head(&ctx->journal.j_wait_reserved);
	init_waitqueue_head(&ctx->journal.j_fc_wait);
	atomic_set(&ctx->journal.j_reserved_credits, 0);
	ctx->journal.j_private = sb;
	ctx->journal.j_blocksize = TEST_BLK_SIZE;
	ctx->journal.j_dev = &ctx->bdev;
	ctx->journal.j_fs_dev = &ctx->bdev;
	ctx->journal.j_flags = JBD2_BARRIER;
	ctx->journal.j_task = current;
	ctx->journal.j_fc_first = 100;
	ctx->journal.j_fc_last = 100 + TEST_MAX_FC_BUFS;
	ctx->journal.j_fc_off = 0;
	ctx->journal.j_fc_wbuf = ctx->fc_wbuf;
	ctx->journal.j_stats.ts_tid = 1;
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->journal.j_submit_inode_data_buffers =
		mock_submit_inode_data_buffers;

	ctx->running_txn.t_tid = 10;
	ctx->running_txn.t_state = T_RUNNING;
	atomic_set(&ctx->running_txn.t_updates, 0);
	ctx->journal.j_running_transaction = &ctx->running_txn;
	ctx->handle.h_transaction = &ctx->running_txn;

	sbi->s_journal = &ctx->journal;
	ext4_fc_init(sb, &ctx->journal);
	ctx->journal.j_fc_cleanup_callback = test_j_fc_cleanup_cb;

	for (i = 0; i < TEST_MAX_FC_BUFS; i++) {
		struct buffer_head *bh = &ctx->fc_bhs[i];

		bh->b_data = ctx->fc_buf_data[i];
		bh->b_size = TEST_BLK_SIZE;
		bh->b_blocknr = 500 + i;
		bh->b_bdev = &ctx->bdev;
		atomic_set(&bh->b_count, 1);
		set_buffer_uptodate(bh);
		set_buffer_mapped(bh);
	}

	for (i = 0; i < TEST_MAX_BLOCKS; i++) {
		struct buffer_head *bh = &ctx->blocks[i].bh;

		bh->b_data = ctx->blocks[i].data;
		bh->b_size = TEST_BLK_SIZE;
		bh->b_blocknr = TEST_FIRST_PBLK + i;
		bh->b_bdev = &ctx->bdev;
		atomic_set(&bh->b_count, 1);
		set_buffer_uptodate(bh);
		set_buffer_mapped(bh);
	}

	ctx->journal_mode = EXT4_INODE_ORDERED_DATA_MODE;

	kunit_activate_static_stub(test, ext4_inode_journal_mode,
				   stub_inode_journal_mode);
	kunit_activate_static_stub(test, ext4_da_update_reserve_space,
				   stub_da_update_reserve_space);
	kunit_activate_static_stub(test, ext4_da_release_space,
				   stub_da_release_space);
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   stub_get_inode_loc);
	kunit_activate_static_stub(test, ext4_get_inode_loc_noio,
				   stub_get_inode_loc_noio);
	kunit_activate_static_stub(test, ext4_get_fc_inode_loc,
				   stub_get_fc_inode_loc);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   stub_handle_dirty_metadata);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   stub_mark_iloc_dirty);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   stub_mark_inode_dirty);
	kunit_activate_static_stub(test, __ext4_ext_dirty,
				   stub_ext_dirty);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   stub_free_blocks);
	kunit_activate_static_stub(test, ext4_orphan_add,
				   stub_orphan_add);
	kunit_activate_static_stub(test, ext4_orphan_del,
				   stub_orphan_del);
	kunit_activate_static_stub(test, ext4_mark_inode_used,
				   stub_mark_inode_used);
	kunit_activate_static_stub(test, ext4_mb_mark_bb,
				   stub_mb_mark_bb);
	kunit_activate_static_stub(test, __ext4_iget,
				   stub_ext4_iget);
	kunit_activate_static_stub(test, ext4_map_blocks,
				   stub_map_blocks);
	kunit_activate_static_stub(test, ext4_getblk,
				   stub_ext4_getblk);
	kunit_activate_static_stub(test, ext4_find_extent,
				   stub_find_extent);
	kunit_activate_static_stub(test, ext4_ext_insert_extent,
				   stub_insert_extent);
	kunit_activate_static_stub(test, ext4_es_lookup_extent,
				   stub_es_lookup_extent);
	kunit_activate_static_stub(test, ext4_fc_get_buf,
				   stub_fc_get_buf);
	kunit_activate_static_stub(test, ext4_fc_submit_bh,
				   stub_fc_submit_bh);
	kunit_activate_static_stub(test, ext4_fc_issue_flush,
				   stub_fc_issue_flush);

	init_mock_inode_slot(ctx, &ctx->inodes[0], TEST_ROOT_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[1], TEST_DIR_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[2], TEST_FILE1_INO,
			     S_IFREG | 0644);
	init_mock_inode_slot(ctx, &ctx->inodes[3], TEST_FILE2_INO,
			     S_IFREG | 0644);
	init_mock_inode_slot(ctx, &ctx->inodes[4], TEST_SUBDIR_INO,
			     S_IFDIR | 0755);

	return 0;
}

/* =========================================================================
 * 1. Initialization, Eligibility, and Snapshot Stat Helpers
 * =========================================================================
 */

static void test_fc_init_and_eligibility(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct inode *inode = get_mock_inode(ctx, TEST_FILE1_INO);
	struct ext4_inode_info *ei = EXT4_I(inode);
	journal_t *tmp_journal;
	atomic64_t max_stat = ATOMIC64_INIT(0);
	int snap_err = EXT4_FC_SNAP_ERR_NONE;

	tmp_journal = kunit_kzalloc(test, sizeof(*tmp_journal), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tmp_journal);

	/*
	 * 1) ext4_fc_init sets j_fc_replay_callback unconditionally, but sets
	 *    j_fc_cleanup_callback only when JOURNAL_FAST_COMMIT is enabled.
	 */
	clear_opt2(sb, JOURNAL_FAST_COMMIT);
	ext4_fc_init(sb, tmp_journal);
	KUNIT_EXPECT_NOT_NULL(test, tmp_journal->j_fc_replay_callback);
	KUNIT_EXPECT_NULL(test, tmp_journal->j_fc_cleanup_callback);

	set_opt2(sb, JOURNAL_FAST_COMMIT);
	ext4_fc_init(sb, tmp_journal);
	KUNIT_EXPECT_NOT_NULL(test, tmp_journal->j_fc_replay_callback);
	KUNIT_EXPECT_NOT_NULL(test, tmp_journal->j_fc_cleanup_callback);

	/* 2) ext4_fc_init_inode clears state and resets ranges */
	ei->i_fc_lblk_start = 42;
	ei->i_fc_lblk_len = 10;
	ext4_set_inode_state(inode, EXT4_STATE_FC_COMMITTING);
	ext4_set_inode_state(inode, EXT4_STATE_FC_REQUEUE);
	ext4_fc_init_inode(inode);
	KUNIT_EXPECT_EQ(test, ei->i_fc_lblk_start, 0U);
	KUNIT_EXPECT_EQ(test, ei->i_fc_lblk_len, 0U);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode, EXT4_STATE_FC_COMMITTING));
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode, EXT4_STATE_FC_REQUEUE));
	KUNIT_EXPECT_TRUE(test, list_empty(&ei->i_fc_list));
	KUNIT_EXPECT_TRUE(test, list_empty(&ei->i_fc_dilist));
	KUNIT_EXPECT_NULL(test, ei->i_fc_snap);

	/* 3) ext4_fc_disabled and ext4_fc_eligible transitions */
	KUNIT_EXPECT_FALSE(test, ext4_fc_disabled_test(sb));
	KUNIT_EXPECT_TRUE(test, ext4_fc_eligible_test(sb));

	ctx->sbi->s_mount_state |= EXT4_FC_REPLAY;
	KUNIT_EXPECT_TRUE(test, ext4_fc_disabled_test(sb));
	KUNIT_EXPECT_FALSE(test, ext4_fc_eligible_test(sb));
	ctx->sbi->s_mount_state &= ~EXT4_FC_REPLAY;

	ext4_set_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	KUNIT_EXPECT_FALSE(test, ext4_fc_disabled_test(sb));
	KUNIT_EXPECT_FALSE(test, ext4_fc_eligible_test(sb));
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);

	clear_opt2(sb, JOURNAL_FAST_COMMIT);
	KUNIT_EXPECT_TRUE(test, ext4_fc_disabled_test(sb));
	KUNIT_EXPECT_FALSE(test, ext4_fc_eligible_test(sb));
	set_opt2(sb, JOURNAL_FAST_COMMIT);

	/* 4) ext4_fc_set_snap_err preserves first error */
	ext4_fc_set_snap_err_test(&snap_err, EXT4_FC_SNAP_ERR_ES_MISS);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_ES_MISS);
	ext4_fc_set_snap_err_test(&snap_err, EXT4_FC_SNAP_ERR_NOMEM);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_ES_MISS);

	/* 5) ext4_fc_snap_stats_update_max monotonically updates max */
	ext4_fc_snap_stats_update_max_test(&max_stat, 100);
	KUNIT_EXPECT_EQ(test, atomic64_read(&max_stat), 100LL);
	ext4_fc_snap_stats_update_max_test(&max_stat, 50);
	KUNIT_EXPECT_EQ(test, atomic64_read(&max_stat), 100LL);
	ext4_fc_snap_stats_update_max_test(&max_stat, 250);
	KUNIT_EXPECT_EQ(test, atomic64_read(&max_stat), 250LL);
}

/* =========================================================================
 * 2. Ineligibility Marking (ext4_fc_mark_ineligible)
 * =========================================================================
 */

static void test_fc_mark_ineligible(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;

	/* 1) No-op when fast commit is disabled */
	clear_opt2(sb, JOURNAL_FAST_COMMIT);
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_XATTR, &ctx->handle);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_XATTR],
			0U);
	set_opt2(sb, JOURNAL_FAST_COMMIT);

	/* 2) Valid handle records t_tid and sets EXT4_MF_FC_INELIGIBLE */
	ctx->running_txn.t_tid = 10;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_XATTR, &ctx->handle);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 10U);
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_XATTR],
			1U);

	/* 3) Higher TID advances s_fc_ineligible_tid; lower TID does not */
	ctx->running_txn.t_tid = 15;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_RESIZE, &ctx->handle);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 15U);
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_RESIZE],
			1U);

	ctx->running_txn.t_tid = 12;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_RESIZE, &ctx->handle);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 15U);
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_RESIZE],
			2U);

	/* 4) NULL handle uses journal->j_running_transaction when present */
	ctx->running_txn.t_tid = 20;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_NOMEM, NULL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 20U);
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_NOMEM],
			1U);

	/* 5) NULL handle with no running transaction sets flag without tid update */
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	sbi->s_fc_ineligible_tid = 0;
	ctx->journal.j_running_transaction = NULL;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_RENAME_DIR, NULL);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 0U);
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_RENAME_DIR],
			1U);

	ctx->journal.j_running_transaction = &ctx->running_txn;
	ctx->running_txn.t_tid = 10;
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
}

/* =========================================================================
 * 3. Inode and Logical Range Tracking
 * =========================================================================
 */

static void test_fc_track_inode_and_range(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct ext4_inode_info *ei2 = EXT4_I(file2);

	/* 1) Directory inodes are ignored by track_inode and track_range */
	ext4_fc_track_inode(&ctx->handle, dir);
	ext4_fc_track_range(&ctx->handle, dir, 0, 5);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(dir)->i_fc_list));

	/* 2) Data journalling inode marks fs ineligible */
	ctx->journal_mode = EXT4_INODE_JOURNAL_DATA_MODE;
	ext4_fc_track_inode(&ctx->handle, file1);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_INODE_JOURNAL_DATA],
			1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ei1->i_fc_list));
	ctx->journal_mode = EXT4_INODE_ORDERED_DATA_MODE;
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	sbi->s_fc_ineligible_tid = 0;

	/* 3) Inline data inode marks fs ineligible on track_range */
	ext4_set_inode_flag(file1, EXT4_INODE_INLINE_DATA);
	ext4_set_inode_state(file1, EXT4_STATE_MAY_INLINE_DATA);
	ei1->i_inline_off = 164;
	ext4_fc_track_range(&ctx->handle, file1, 0, 1);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_XATTR],
			1U);
	ext4_clear_inode_flag(file1, EXT4_INODE_INLINE_DATA);
	ext4_clear_inode_state(file1, EXT4_STATE_MAY_INLINE_DATA);
	ei1->i_inline_off = 0;
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	sbi->s_fc_ineligible_tid = 0;

	/* 4) Special inode (< EXT4_FIRST_INO) does not record range */
	file1->i_ino = EXT4_ROOT_INO;
	ext4_fc_track_range(&ctx->handle, file1, 5, 10);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 0U);
	list_del_init(&ei1->i_fc_list);
	file1->i_ino = TEST_FILE1_INO;
	ei1->i_sync_tid = 0;

	/*
	 * 5) Range tracking within the same TID merges disjoint and overlapping
	 *    ranges into their bounding interval, and subsequent track_inode
	 *    in the same TID preserves the range.
	 */
	ctx->running_txn.t_tid = 10;
	ext4_fc_track_inode(&ctx->handle, file1);
	KUNIT_EXPECT_EQ(test, ei1->i_sync_tid, 10U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 0U);
	KUNIT_EXPECT_FALSE(test, list_empty(&ei1->i_fc_list));

	/* Initial range [10, 20] -> start=10, len=11 */
	ext4_fc_track_range(&ctx->handle, file1, 10, 20);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 10U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 11U);

	/* Expand right [25, 30] -> [10, 30], start=10, len=21 */
	ext4_fc_track_range(&ctx->handle, file1, 25, 30);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 10U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 21U);

	/* Expand left [4, 8] -> [4, 30], start=4, len=27 */
	ext4_fc_track_range(&ctx->handle, file1, 4, 8);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 4U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 27U);

	/* Contained sub-range [12, 18] -> unchanged [4, 30] */
	ext4_fc_track_range(&ctx->handle, file1, 12, 18);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 4U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 27U);

	/* Calling track_inode again in same TID keeps tracked range intact */
	ext4_fc_track_inode(&ctx->handle, file1);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 4U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 27U);

	/*
	 * 6) Tracking while EXT4_STATE_FC_COMMITTING is set marks the inode
	 *    with EXT4_STATE_FC_REQUEUE, and tracking in a new TID resets the
	 *    previous range.
	 */
	ext4_set_inode_state(file1, EXT4_STATE_FC_COMMITTING);
	ctx->running_txn.t_tid = 11;
	ext4_fc_track_range(&ctx->handle, file1, 50, 55);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(file1, EXT4_STATE_FC_REQUEUE));
	KUNIT_EXPECT_EQ(test, ei1->i_sync_tid, 11U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_start, 50U);
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 6U);
	ext4_clear_inode_state(file1, EXT4_STATE_FC_COMMITTING);
	ext4_clear_inode_state(file1, EXT4_STATE_FC_REQUEUE);

	/*
	 * 7) Tracking a new inode while JBD2_FAST_COMMIT_ONGOING is set
	 *    enqueues it on FC_Q_STAGING rather than FC_Q_MAIN.
	 */
	ctx->journal.j_flags |= JBD2_FAST_COMMIT_ONGOING;
	ext4_fc_track_inode(&ctx->handle, file2);
	KUNIT_EXPECT_FALSE(test, list_empty(&sbi->s_fc_q[FC_Q_STAGING]));
	KUNIT_EXPECT_PTR_EQ(test,
			    list_first_entry(&sbi->s_fc_q[FC_Q_STAGING],
					     struct ext4_inode_info, i_fc_list),
			    ei2);
	ctx->journal.j_flags &= ~JBD2_FAST_COMMIT_ONGOING;
}

/* =========================================================================
 * 4. Dentry Update Tracking and Inode Deletion (ext4_fc_del)
 * =========================================================================
 */

struct fc_del_wait_work {
	struct work_struct work;
	struct inode *inode;
};

static void fc_del_unblock_worker(struct work_struct *work)
{
	struct fc_del_wait_work *w =
		container_of(work, struct fc_del_wait_work, work);
	struct inode *inode = w->inode;
	int alloc_ctx;

	/*
	 * Simulate the commit thread finishing data flush and transitioning
	 * FLUSHING_DATA -> COMMITTING -> done while ext4_fc_del() is waiting.
	 */
	alloc_ctx = ext4_fc_lock(inode->i_sb);
	ext4_clear_inode_state(inode, EXT4_STATE_FC_FLUSHING_DATA);
	ext4_set_inode_state(inode, EXT4_STATE_FC_COMMITTING);
	ext4_fc_wake_inode_state_test(inode, EXT4_STATE_FC_FLUSHING_DATA);
	ext4_fc_unlock(inode->i_sb, alloc_ctx);

	alloc_ctx = ext4_fc_lock(inode->i_sb);
	ext4_clear_inode_state(inode, EXT4_STATE_FC_COMMITTING);
	ext4_fc_wake_inode_state_test(inode, EXT4_STATE_FC_COMMITTING);
	ext4_fc_unlock(inode->i_sb, alloc_ctx);
}

static void test_fc_track_dentry_and_del(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct dentry *dentries;
	struct dentry *d_parent, *d_f1, *d_f1_link, *d_f2;
	struct ext4_fc_dentry_update *fcd;
	struct fc_del_wait_work wait_work;

	dentries = kunit_kcalloc(test, 4, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	d_parent = &dentries[0];
	d_f1 = &dentries[1];
	d_f1_link = &dentries[2];
	d_f2 = &dentries[3];

	init_mock_dentry(d_parent, NULL, "dir", dir, sb);
	init_mock_dentry(d_f1, d_parent, "file1", file1, sb);
	init_mock_dentry(d_f1_link, d_parent, "file1_ln", file1, sb);
	init_mock_dentry(d_f2, d_parent, "file2", file2, sb);

	/* 1) Encrypted parent directory marks fs ineligible */
	dir->i_flags |= S_ENCRYPTED;
	ext4_fc_track_create(&ctx->handle, d_f1);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test,
			sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_REASON_ENCRYPTED_FILENAME],
			1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&sbi->s_fc_dentry_q[FC_Q_MAIN]));
	dir->i_flags &= ~S_ENCRYPTED;
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	sbi->s_fc_ineligible_tid = 0;

	/* 2) Track create, link, and unlink onto MAIN and STAGING queues */
	ext4_fc_track_create(&ctx->handle, d_f1);
	KUNIT_EXPECT_FALSE(test, list_empty(&sbi->s_fc_dentry_q[FC_Q_MAIN]));
	KUNIT_EXPECT_FALSE(test, list_empty(&ei1->i_fc_dilist));

	fcd = list_first_entry(&sbi->s_fc_dentry_q[FC_Q_MAIN],
			       struct ext4_fc_dentry_update, fcd_list);
	KUNIT_EXPECT_EQ(test, fcd->fcd_op, EXT4_FC_TAG_CREAT);
	KUNIT_EXPECT_EQ(test, fcd->fcd_parent, (int)TEST_DIR_INO);
	KUNIT_EXPECT_EQ(test, fcd->fcd_ino, (int)TEST_FILE1_INO);
	KUNIT_EXPECT_STREQ(test, (const char *)fcd->fcd_name.name.name, "file1");

	ext4_fc_track_link(&ctx->handle, file1, d_f1_link);
	ext4_fc_track_unlink(&ctx->handle, d_f1_link);

	ctx->journal.j_flags |= JBD2_FULL_COMMIT_ONGOING;
	ext4_fc_track_create(&ctx->handle, d_f2);
	KUNIT_EXPECT_FALSE(test, list_empty(&sbi->s_fc_dentry_q[FC_Q_STAGING]));
	ctx->journal.j_flags &= ~JBD2_FULL_COMMIT_ONGOING;

	/*
	 * 3) ext4_fc_del removes file2's CREAT entry from s_fc_dentry_q and
	 *    i_fc_dilist even when file2 is not on s_fc_q.
	 */
	ext4_fc_del(file2);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(file2)->i_fc_dilist));
	KUNIT_EXPECT_TRUE(test, list_empty(&sbi->s_fc_dentry_q[FC_Q_STAGING]));

	/*
	 * 4) ext4_fc_del waits for EXT4_STATE_FC_FLUSHING_DATA and
	 *    EXT4_STATE_FC_COMMITTING to clear before deleting file1.
	 */
	ext4_fc_track_inode(&ctx->handle, file1);
	ext4_set_inode_state(file1, EXT4_STATE_FC_FLUSHING_DATA);
	wait_work.inode = file1;
	INIT_WORK_ONSTACK(&wait_work.work, fc_del_unblock_worker);
	schedule_work(&wait_work.work);

	ext4_fc_del(file1);
	flush_work(&wait_work.work);
	destroy_work_on_stack(&wait_work.work);

	KUNIT_EXPECT_TRUE(test, list_empty(&ei1->i_fc_list));
	KUNIT_EXPECT_TRUE(test, list_empty(&ei1->i_fc_dilist));
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(file1, EXT4_STATE_FC_FLUSHING_DATA));
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(file1, EXT4_STATE_FC_COMMITTING));
}

/* =========================================================================
 * 5. Space Reservation, Padding, and TLV Writing
 * =========================================================================
 */

static void test_fc_reserve_space_and_tlv_writing(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct ext4_fc_tl tl;
	struct ext4_fc_tail tail;
	u8 payload[600];
	u8 *dst;
	u32 crc = 0;

	memset(payload, 0xAB, sizeof(payload));

	/* 1) Request exceeding bsize - EXT4_FC_TAG_BASE_LEN fails immediately */
	dst = ext4_fc_reserve_space_test(sb,
					 TEST_BLK_SIZE - EXT4_FC_TAG_BASE_LEN + 1,
					 &crc);
	KUNIT_EXPECT_NULL(test, dst);
	KUNIT_EXPECT_EQ(test, ctx->fc_get_buf_calls, 0);

	/* 2) ext4_fc_get_buf failure on initial block returns NULL */
	ctx->fail_fc_get_buf = -ENOSPC;
	dst = ext4_fc_reserve_space_test(sb, 32, &crc);
	KUNIT_EXPECT_NULL(test, dst);
	ctx->fail_fc_get_buf = 0;

	/*
	 * 3) Add a 600-byte TLV into block 0 (takes 604 bytes), then add
	 *    another 600-byte TLV. The second TLV does not fit in the
	 *    remaining 420 bytes of block 0, so ext4_fc_reserve_space writes
	 *    an EXT4_FC_TAG_PAD TLV of length 416 (420 - 4), submits block 0,
	 *    and allocates block 1.
	 */
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD,
					       sizeof(payload), payload, &crc));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes,
			(unsigned int)(EXT4_FC_TAG_BASE_LEN + sizeof(payload)));
	KUNIT_EXPECT_EQ(test, ctx->fc_submit_bh_calls, 0U);

	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD,
					       sizeof(payload), payload, &crc));
	KUNIT_EXPECT_EQ(test, ctx->fc_submit_bh_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->fc_submit_tail_calls, 0U);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes,
			(unsigned int)(TEST_BLK_SIZE + EXT4_FC_TAG_BASE_LEN +
				       sizeof(payload)));

	/* Verify PAD TLV was written at offset 604 of block 0 */
	memcpy(&tl, ctx->fc_buf_data[0] + EXT4_FC_TAG_BASE_LEN + sizeof(payload),
	       EXT4_FC_TAG_BASE_LEN);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(tl.fc_tag), (u16)EXT4_FC_TAG_PAD);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(tl.fc_len),
			(u16)(TEST_BLK_SIZE - 2 * EXT4_FC_TAG_BASE_LEN -
			      sizeof(payload)));

	/* 4) Write tail tag into block 1 and verify rounding + tail submission */
	KUNIT_EXPECT_EQ(test, ext4_fc_write_tail_test(sb, crc), 0);
	KUNIT_EXPECT_EQ(test, ctx->fc_submit_bh_calls, 2U);
	KUNIT_EXPECT_EQ(test, ctx->fc_submit_tail_calls, 1U);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes, (unsigned int)(2 * TEST_BLK_SIZE));

	memcpy(&tl, ctx->fc_buf_data[1] + EXT4_FC_TAG_BASE_LEN + sizeof(payload),
	       EXT4_FC_TAG_BASE_LEN);
	memcpy(&tail,
	       ctx->fc_buf_data[1] + 2 * EXT4_FC_TAG_BASE_LEN + sizeof(payload),
	       sizeof(tail));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(tl.fc_tag), (u16)EXT4_FC_TAG_TAIL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(tail.fc_tid), 10U);

	/* 5) Missing inode snapshot increments snap_fail_no_snap */
	KUNIT_EXPECT_EQ(test, ext4_fc_write_inode_test(file1, &crc),
			-ECANCELED);
	KUNIT_EXPECT_EQ(test, ext4_fc_write_inode_data_test(file1, &crc),
			-ECANCELED);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_no_snap),
			2LL);
}

/* =========================================================================
 * 6. Inode and Extent Status Range Snapshotting
 * =========================================================================
 */

static void test_fc_snapshot_inode_and_ranges(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct dentry d_parent, d_f2;
	struct inode **inodes = NULL;
	unsigned int nr_inodes = 0, nr_ranges = 0;
	int snap_err = EXT4_FC_SNAP_ERR_NONE;
	u32 crc = 0;

	/* 1) ext4_get_inode_loc_noio failure records EXT4_FC_SNAP_ERR_INODE_LOC */
	ctx->fail_get_inode_loc_noio = -EAGAIN;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, &nr_ranges,
						    &snap_err),
			-EAGAIN);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_INODE_LOC);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_inode_loc),
			1LL);
	ctx->fail_get_inode_loc_noio = 0;

	/* 2) ES miss on tracked range records EXT4_FC_SNAP_ERR_ES_MISS */
	snap_err = EXT4_FC_SNAP_ERR_NONE;
	ext4_fc_track_range(&ctx->handle, file1, 0, 3);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, &nr_ranges,
						    &snap_err),
			-EAGAIN);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_ES_MISS);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_es_miss),
			1LL);

	/* 3) Delayed extent in ES tree records EXT4_FC_SNAP_ERR_ES_DELAYED */
	snap_err = EXT4_FC_SNAP_ERR_NONE;
	down_write(&ei1->i_data_sem);
	ext4_es_insert_delayed_extent(file1, 0, 4, false, false);
	up_write(&ei1->i_data_sem);
	ext4_fc_track_range(&ctx->handle, file1, 0, 3);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, &nr_ranges,
						    &snap_err),
			-EAGAIN);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_ES_DELAYED);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_es_delayed),
			1LL);
	down_write(&ei1->i_data_sem);
	ext4_es_remove_extent(file1, 0, EXT_MAX_BLOCKS);
	up_write(&ei1->i_data_sem);

	/* 4) Unsupported ES status records EXT4_FC_SNAP_ERR_ES_OTHER */
	snap_err = EXT4_FC_SNAP_ERR_NONE;
	ctx->use_custom_es_lookup = true;
	ctx->custom_es_ret = 1;
	ctx->custom_es.es_lblk = 0;
	ctx->custom_es.es_len = 4;
	ctx->custom_es.es_pblk = 0;
	ext4_es_set_referenced(&ctx->custom_es);
	ext4_fc_track_range(&ctx->handle, file1, 0, 3);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, &nr_ranges,
						    &snap_err),
			-EAGAIN);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_ES_OTHER);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_es_other),
			1LL);
	ctx->use_custom_es_lookup = false;

	/* 5) Exceeding EXT4_FC_SNAPSHOT_MAX_RANGES returns -E2BIG */
	snap_err = EXT4_FC_SNAP_ERR_NONE;
	down_write(&ei1->i_data_sem);
	ext4_es_insert_extent(file1, 0, 4, 200, EXTENT_STATUS_WRITTEN, false);
	up_write(&ei1->i_data_sem);
	ext4_fc_track_range(&ctx->handle, file1, 0, 3);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 2048, &nr_ranges,
						    &snap_err),
			-E2BIG);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_RANGES_CAP);
	KUNIT_EXPECT_EQ(test,
			atomic64_read(&sbi->s_fc_snap_stats.snap_fail_ranges_cap),
			1LL);

	/*
	 * 6) Snapshot mixed written, hole, unwritten, and large (>32768)
	 *    extents, verifying automatic splitting at EXT_INIT_MAX_LEN (32768)
	 *    and EXT_UNWRITTEN_MAX_LEN (32767).
	 */
	down_write(&ei1->i_data_sem);
	ext4_es_remove_extent(file1, 0, EXT_MAX_BLOCKS);
	/* [0, 3]: written at pblk 200 */
	ext4_es_insert_extent(file1, 0, 4, 200, EXTENT_STATUS_WRITTEN, false);
	/* [4, 5]: hole */
	ext4_es_insert_extent(file1, 4, 2, 0, EXTENT_STATUS_HOLE, false);
	/* [6, 40005]: 40000 unwritten blocks at pblk 1000 (splits into 32767 + 7233) */
	ext4_es_insert_extent(file1, 6, 40000, 1000, EXTENT_STATUS_UNWRITTEN,
			      false);
	up_write(&ei1->i_data_sem);

	snap_err = EXT4_FC_SNAP_ERR_NONE;
	ext4_fc_track_range(&ctx->handle, file1, 1, 40005);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, &nr_ranges,
						    &snap_err),
			0);
	/* 1 written [1..3] + 1 hole [4..5] + 2 split unwritten [6..32772, 32773..40005] = 4 */
	KUNIT_EXPECT_EQ(test, nr_ranges, 4U);
	KUNIT_EXPECT_NOT_NULL(test, ei1->i_fc_snap);

	/* Serialize the snapshotted inode and ranges into fast commit buffer */
	sbi->s_fc_bytes = 0;
	ctx->journal.j_fc_off = 0;
	KUNIT_EXPECT_EQ(test, ext4_fc_write_inode_data_test(file1, &crc), 0);
	KUNIT_EXPECT_EQ(test, ext4_fc_write_inode_test(file1, &crc), 0);

	/*
	 * 7) Count and snapshot both s_fc_q[MAIN] inodes and create-only
	 *    inodes from s_fc_dentry_q[MAIN].
	 */
	init_mock_dentry(&d_parent, NULL, "dir", dir, sb);
	init_mock_dentry(&d_f2, &d_parent, "file2", file2, sb);
	ext4_fc_track_create(&ctx->handle, &d_f2);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(file2)->i_fc_list));
	KUNIT_EXPECT_EQ(test, ext4_fc_count_snapshot_inodes_test(sb), 2U);

	KUNIT_EXPECT_EQ(test,
			ext4_fc_alloc_snapshot_inodes_test(sb, &inodes,
							   &nr_inodes),
			0);
	KUNIT_EXPECT_EQ(test, nr_inodes, 2U);
	KUNIT_ASSERT_NOT_NULL(test, inodes);

	/* Capacity smaller than queued inode count returns -E2BIG */
	snap_err = EXT4_FC_SNAP_ERR_NONE;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inodes_test(&ctx->journal, inodes, 1,
						     &nr_inodes, &nr_ranges,
						     &snap_err),
			-E2BIG);
	KUNIT_EXPECT_EQ(test, snap_err, (int)EXT4_FC_SNAP_ERR_INODES_CAP);

	snap_err = EXT4_FC_SNAP_ERR_NONE;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_snapshot_inodes_test(&ctx->journal, inodes, 2,
						     &nr_inodes, &nr_ranges,
						     &snap_err),
			0);
	KUNIT_EXPECT_EQ(test, nr_inodes, 2U);
	/* Create-only inode (file2) is marked EXT4_STATE_FC_COMMITTING */
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(file2, EXT4_STATE_FC_COMMITTING));
	KUNIT_EXPECT_NOT_NULL(test, EXT4_I(file2)->i_fc_snap);
	kvfree(inodes);
}

/* =========================================================================
 * 7. Fast Commit Cleanup, Stats, and Procfs Output (ext4_fc_info_show)
 * =========================================================================
 */

static void test_fc_cleanup_and_stats(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct ext4_inode_info *ei2 = EXT4_I(file2);
	struct seq_file seq = {};
	char *buf;

	/*
	 * 1) Fast commit cleanup (full = 0):
	 *    - file1 (no FC_REQUEUE) is removed from FC_Q_MAIN and reset.
	 *    - file2 (FC_REQUEUE set) is requeued and preserves its range.
	 */
	ctx->running_txn.t_tid = 10;
	ext4_fc_track_range(&ctx->handle, file1, 2, 5);
	ext4_fc_track_range(&ctx->handle, file2, 8, 12);
	ext4_set_inode_state(file1, EXT4_STATE_FC_COMMITTING);
	ext4_set_inode_state(file2, EXT4_STATE_FC_COMMITTING);
	ext4_set_inode_state(file2, EXT4_STATE_FC_REQUEUE);
	sbi->s_fc_bytes = 512;

	ext4_fc_cleanup_test(&ctx->journal, 0, 10);
	KUNIT_EXPECT_TRUE(test, list_empty(&ei1->i_fc_list));
	KUNIT_EXPECT_EQ(test, ei1->i_fc_lblk_len, 0U);
	KUNIT_EXPECT_FALSE(test, list_empty(&ei2->i_fc_list));
	KUNIT_EXPECT_EQ(test, ei2->i_fc_lblk_start, 8U);
	KUNIT_EXPECT_EQ(test, ei2->i_fc_lblk_len, 5U);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(file2, EXT4_STATE_FC_REQUEUE));
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(file2, EXT4_STATE_FC_COMMITTING));
	/* Fast commit cleanup does not reset s_fc_bytes */
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes, 512U);

	/*
	 * 2) Full commit cleanup (full = 1):
	 *    - Inodes with i_sync_tid > tid are requeued; inodes with
	 *      i_sync_tid <= tid are removed and reset.
	 *    - s_fc_ineligible_tid is cleared only when tid >= s_fc_ineligible_tid.
	 *    - s_fc_bytes is reset to 0.
	 */
	ctx->running_txn.t_tid = 15;
	ext4_fc_track_range(&ctx->handle, file1, 1, 3);
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_XATTR, &ctx->handle);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 15U);

	/* Full commit of TID 10: file2 (tid 10) removed, file1 (tid 15) kept */
	ext4_fc_cleanup_test(&ctx->journal, 1, 10);
	KUNIT_EXPECT_TRUE(test, list_empty(&ei2->i_fc_list));
	KUNIT_EXPECT_FALSE(test, list_empty(&ei1->i_fc_list));
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes, 0U);

	/* Full commit of TID 15: file1 removed and ineligibility cleared */
	ext4_fc_cleanup_test(&ctx->journal, 1, 15);
	KUNIT_EXPECT_TRUE(test, list_empty(&ei1->i_fc_list));
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_ineligible_tid, 0U);

	/*
	 * 3) ext4_fc_update_stats and ext4_fc_info_show formatting
	 */
	memset(&sbi->s_fc_stats, 0, sizeof(sbi->s_fc_stats));
	ext4_fc_update_stats_test(sb, EXT4_FC_STATUS_OK, 40000, 2, 10);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_num_commits, 1UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_numblks, 2UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.s_fc_avg_commit_time, 40000ULL);

	/* EWMA update: (80000 + 40000 * 3) / 4 = 50000 */
	ext4_fc_update_stats_test(sb, EXT4_FC_STATUS_OK, 80000, 3, 10);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_num_commits, 2UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_numblks, 5UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.s_fc_avg_commit_time, 50000ULL);

	ext4_fc_update_stats_test(sb, EXT4_FC_STATUS_INELIGIBLE, 0, 0, 10);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_ineligible_commits, 1UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 0UL);

	ext4_fc_update_stats_test(sb, EXT4_FC_STATUS_FAILED, 0, 0, 10);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_ineligible_commits, 2UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 1UL);

	ext4_fc_update_stats_test(sb, EXT4_FC_STATUS_SKIPPED, 0, 0, 10);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_skipped_commits, 1UL);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	seq.buf = buf;
	seq.size = PAGE_SIZE;
	seq.private = sb;

	/* Non-start token produces no output */
	KUNIT_EXPECT_EQ(test, ext4_fc_info_show(&seq, NULL), 0);
	KUNIT_EXPECT_EQ(test, seq.count, 0UL);

	/* SEQ_START_TOKEN outputs full stats table */
	KUNIT_EXPECT_EQ(test, ext4_fc_info_show(&seq, SEQ_START_TOKEN), 0);
	KUNIT_EXPECT_GT(test, seq.count, 0UL);
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "fc stats:\n2 commits\n2 ineligible\n5 numblks\n50us avg_commit_time"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "Ineligible reasons:"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "Snapshot stats:"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "Snapshot failures:"));
}

/* =========================================================================
 * 8. Top-Level Commit Paths and Fallbacks (ext4_fc_commit)
 * =========================================================================
 */

static void test_fc_commit_paths(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct ext4_fc_tl tl;

	/* 1) Already-committed TID is skipped (-EALREADY) */
	ctx->journal.j_commit_sequence = 10;
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_skipped_commits, 1UL);

	/* 1b) JOURNAL_FAST_COMMIT disabled delegates to jbd2_complete_transaction */
	clear_opt2(sb, JOURNAL_FAST_COMMIT);
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_skipped_commits, 1UL);
	set_opt2(sb, JOURNAL_FAST_COMMIT);

	/* 1c) jbd2_fc_begin_commit failure (ts_tid == 0 -> -EINVAL) records FAILED */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 10;
	ctx->journal.j_stats.ts_tid = 0;
	ctx->journal.j_running_transaction = NULL;
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 1UL);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_ineligible_commits, 1UL);
	ctx->journal.j_stats.ts_tid = 1;
	ctx->journal.j_running_transaction = &ctx->running_txn;
	sbi->s_fc_stats.fc_failed_commits = 0;
	sbi->s_fc_stats.fc_ineligible_commits = 0;

	/* 2) Ineligible filesystem falls back to full commit */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->running_txn.t_tid = 10;
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_XATTR, &ctx->handle);
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_ineligible_commits, 1UL);
	ctx->journal.j_flags &= ~JBD2_FULL_COMMIT_ONGOING;
	ext4_clear_mount_flag(sb, EXT4_MF_FC_INELIGIBLE);
	sbi->s_fc_ineligible_tid = 0;

	/* 3) Data flush failure (-EIO) clears FLUSHING_DATA and falls back */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->running_txn.t_tid = 10;
	ext4_fc_track_inode(&ctx->handle, file1);
	WRITE_ONCE(ei1->jinode->i_flags, JI_WRITE_DATA);
	ctx->submit_inode_data_err = -EIO;
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(file1, EXT4_STATE_FC_FLUSHING_DATA));
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 1UL);
	ctx->journal.j_flags &= ~JBD2_FULL_COMMIT_ONGOING;
	WRITE_ONCE(ei1->jinode->i_flags, 0);
	ctx->submit_inode_data_err = 0;

	/* 4) Snapshot ES miss (-EAGAIN) records INELIGIBLE status and falls back */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->running_txn.t_tid = 10;
	ext4_fc_track_range(&ctx->handle, file1, 0, 1);
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_ineligible_commits, 3UL);
	/* Failed commits count does not increment on -EAGAIN */
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 1UL);
	ctx->journal.j_flags &= ~JBD2_FULL_COMMIT_ONGOING;

	/* 5) jbd2_fc_wait_bufs I/O failure (-EIO) records FAILED status */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->running_txn.t_tid = 10;
	ctx->journal.j_fc_off = 0;
	sbi->s_fc_bytes = 0;
	ext4_fc_track_inode(&ctx->handle, file1);
	ctx->fail_fc_wait_bufs = true;
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_stats.fc_failed_commits, 2UL);
	ctx->journal.j_flags &= ~JBD2_FULL_COMMIT_ONGOING;
	ctx->fail_fc_wait_bufs = false;

	/*
	 * 6) Two consecutive successful fast commits in the same TID, with an
	 *    external journal device (j_fs_dev != j_dev) triggering cache flush.
	 */
	ctx->journal.j_commit_sequence = 9;
	ctx->journal.j_commit_request = 9;
	ctx->running_txn.t_tid = 10;
	ctx->journal.j_fc_off = 0;
	sbi->s_fc_bytes = 0;
	atomic_set(&sbi->s_fc_subtid, 0);
	ctx->journal.j_fs_dev = &ctx->ext_jdev;
	ctx->fc_flush_calls = 0;

	down_write(&ei1->i_data_sem);
	ext4_es_insert_extent(file1, 0, 2, 200, EXTENT_STATUS_WRITTEN, false);
	up_write(&ei1->i_data_sem);
	ext4_fc_track_range(&ctx->handle, file1, 0, 1);

	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&sbi->s_fc_subtid), 1);
	KUNIT_EXPECT_EQ(test, ctx->fc_flush_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, ctx->last_flushed_bdev, &ctx->ext_jdev);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes, (unsigned int)TEST_BLK_SIZE);

	/* Block 0 starts with EXT4_FC_TAG_HEAD */
	memcpy(&tl, ctx->fc_buf_data[0], EXT4_FC_TAG_BASE_LEN);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(tl.fc_tag), (u16)EXT4_FC_TAG_HEAD);

	/* Second fast commit in the same TID (block 1 starts with INODE, not HEAD) */
	ext4_fc_track_inode(&ctx->handle, file1);
	KUNIT_EXPECT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&sbi->s_fc_subtid), 2);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_bytes, (unsigned int)(2 * TEST_BLK_SIZE));
	memcpy(&tl, ctx->fc_buf_data[1], EXT4_FC_TAG_BASE_LEN);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(tl.fc_tag), (u16)EXT4_FC_TAG_INODE);

	ctx->journal.j_fs_dev = &ctx->bdev;
}

/* =========================================================================
 * 9. Replay Regions, Modified Inode Tracking, and Scan Validation
 * =========================================================================
 */

static void test_fc_replay_regions_and_scan(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct ext4_fc_replay_state *state = &sbi->s_fc_replay_state;
	struct ext4_fc_tl tl;
	struct ext4_fc_head head;
	int i;

	/*
	 * 1) ext4_fc_record_regions and ext4_fc_replay_check_excluded across
	 *    dynamic krealloc (> EXT4_FC_REPLAY_REALLOC_INCREMENT = 4)
	 */
	for (i = 0; i < 6; i++) {
		KUNIT_EXPECT_EQ(test,
				ext4_fc_record_regions(sb, 12 + i, i * 10,
						       100 + i * 10, 5, 1),
				0);
	}
	KUNIT_EXPECT_EQ(test, state->fc_regions_used, 6);
	KUNIT_EXPECT_EQ(test, state->fc_regions_valid, 6);
	KUNIT_EXPECT_GE(test, state->fc_regions_size, 6);

	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 100));
	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 104));
	KUNIT_EXPECT_FALSE(test, ext4_fc_replay_check_excluded(sb, 105));
	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 152));

	/* Unvalidated scan region (replay = 0) is not excluded until validated */
	KUNIT_EXPECT_EQ(test,
			ext4_fc_record_regions(sb, 99, 0, 500, 10, 0),
			0);
	KUNIT_EXPECT_EQ(test, state->fc_regions_used, 7);
	KUNIT_EXPECT_EQ(test, state->fc_regions_valid, 6);
	KUNIT_EXPECT_FALSE(test, ext4_fc_replay_check_excluded(sb, 502));

	/* Next replay=1 addition resyncs fc_regions_used to fc_regions_valid */
	KUNIT_EXPECT_EQ(test,
			ext4_fc_record_regions(sb, 100, 0, 600, 4, 1),
			0);
	KUNIT_EXPECT_EQ(test, state->fc_regions_used, 7);
	KUNIT_EXPECT_EQ(test, state->fc_regions_valid, 7);
	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 601));

	/* 2) ext4_fc_record_modified_inode deduplication and krealloc growth */
	for (i = 0; i < 6; i++) {
		KUNIT_EXPECT_EQ(test,
				ext4_fc_record_modified_inode_test(sb, 20 + i),
				0);
		/* Duplicate ino is ignored */
		KUNIT_EXPECT_EQ(test,
				ext4_fc_record_modified_inode_test(sb, 20 + i),
				0);
	}
	KUNIT_EXPECT_EQ(test, state->fc_modified_inodes_used, 6);

	ext4_fc_replay_cleanup(sb);
	memset(state, 0, sizeof(*state));

	/* 3) ext4_fc_value_len_isvalid_test and tag2str */
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_ADD_RANGE,
							 sizeof(struct ext4_fc_add_range)));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_ADD_RANGE, 1));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_DEL_RANGE,
							 sizeof(struct ext4_fc_del_range)));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_CREAT,
							 sizeof(struct ext4_fc_dentry_info) + 1));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_CREAT,
							  sizeof(struct ext4_fc_dentry_info)));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_LINK,
							  sizeof(struct ext4_fc_dentry_info) +
							  EXT4_NAME_LEN + 1));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_INODE,
							 sizeof(struct ext4_fc_inode) +
							 EXT4_GOOD_OLD_INODE_SIZE));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_INODE,
							  sizeof(struct ext4_fc_inode) +
							  TEST_INODE_SIZE + 1));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_PAD, 0));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_TAIL,
							 sizeof(struct ext4_fc_tail)));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_TAIL,
							  sizeof(struct ext4_fc_tail) - 1));
	KUNIT_EXPECT_TRUE(test,
			  ext4_fc_value_len_isvalid_test(sbi, EXT4_FC_TAG_HEAD,
							 sizeof(struct ext4_fc_head)));
	KUNIT_EXPECT_FALSE(test,
			   ext4_fc_value_len_isvalid_test(sbi, 0x9999, 8));
	KUNIT_EXPECT_STREQ(test, tag2str(EXT4_FC_TAG_HEAD), "HEAD");
	KUNIT_EXPECT_STREQ(test, tag2str(0x9999), "ERROR");

	/* 4) Scan error cases */
	/* Block 0 not starting with HEAD returns 0 (early stop) */
	memset(ctx->fc_buf_data[0], 0, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			0);

	/* Unexpected block offset returns -EFSCORRUPTED */
	state->fc_replay_expected_off = 2;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 1, 10),
			-EFSCORRUPTED);

	/* HEAD with unsupported features returns -EOPNOTSUPP */
	memset(state, 0, sizeof(*state));
	memset(ctx->fc_buf_data[0], 0, TEST_BLK_SIZE);
	tl.fc_tag = cpu_to_le16(EXT4_FC_TAG_HEAD);
	tl.fc_len = cpu_to_le16(sizeof(head));
	head.fc_features = cpu_to_le32(0x1);
	head.fc_tid = cpu_to_le32(10);
	memcpy(ctx->fc_buf_data[0], &tl, EXT4_FC_TAG_BASE_LEN);
	memcpy(ctx->fc_buf_data[0] + EXT4_FC_TAG_BASE_LEN, &head, sizeof(head));
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			-EOPNOTSUPP);

	/* HEAD with wrong TID returns JBD2_FC_REPLAY_STOP */
	memset(state, 0, sizeof(*state));
	head.fc_features = 0;
	head.fc_tid = cpu_to_le32(99);
	memcpy(ctx->fc_buf_data[0] + EXT4_FC_TAG_BASE_LEN, &head, sizeof(head));
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			JBD2_FC_REPLAY_STOP);

	/*
	 * Corrupted TAIL CRC on first commit stops cleanly with
	 * JBD2_FC_REPLAY_STOP without failing journal recovery.
	 */
	memset(state, 0, sizeof(*state));
	sbi->s_fc_bytes = 0;
	ctx->journal.j_fc_off = 0;
	ctx->running_txn.t_tid = 10;
	head.fc_tid = cpu_to_le32(10);
	{
		u32 crc = 0;

		ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD, sizeof(head),
				     (u8 *)&head, &crc);
		ext4_fc_write_tail_test(sb, crc);
	}
	/* Corrupt last byte of tail.fc_crc */
	ctx->fc_buf_data[0][2 * EXT4_FC_TAG_BASE_LEN + sizeof(head) +
			    offsetof(struct ext4_fc_tail, fc_crc)] ^= 0xFF;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			JBD2_FC_REPLAY_STOP);
	KUNIT_EXPECT_EQ(test, state->fc_replay_num_tags, 0);

	/*
	 * 5) Valid first commit in block 0 followed by a corrupted second
	 *    commit in block 1 stops cleanly with JBD2_FC_REPLAY_STOP and
	 *    keeps fc_replay_num_tags from block 0.
	 */
	memset(state, 0, sizeof(*state));
	sbi->s_fc_bytes = 0;
	ctx->journal.j_fc_off = 0;
	{
		u32 crc = 0;

		ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD, sizeof(head),
				     (u8 *)&head, &crc);
		ext4_fc_write_tail_test(sb, crc);
	}
	/* Block 1 has an invalid TLV (tag=0) */
	memset(ctx->fc_buf_data[1], 0, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			JBD2_FC_REPLAY_CONTINUE);
	KUNIT_EXPECT_EQ(test, state->fc_replay_num_tags, 2);
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[1],
					    PASS_SCAN, 1, 10),
			JBD2_FC_REPLAY_STOP);
	KUNIT_EXPECT_EQ(test, state->fc_replay_num_tags, 2);
	ext4_fc_replay_cleanup(sb);
	memset(state, 0, sizeof(*state));
}

/* =========================================================================
 * 10. Replay of Inode, Create, Link, and Unlink Tags
 * =========================================================================
 */

static void test_fc_replay_inode_and_dentries(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *root = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *subdir = get_mock_inode(ctx, TEST_SUBDIR_INO);
	struct fc_mock_inode_slot *f1_slot = find_slot(ctx, file1);
	struct dentry *dentries;
	struct dentry *d_dir, *d_f1, *d_f1_link, *d_subdir, *d_f1_overwrite;
	struct ext4_dir_entry_2 *de = NULL;
	struct buffer_head *bh;
	struct qstr q_f1 = QSTR_INIT("file1", 5);
	struct qstr q_f1_ln = QSTR_INIT("file1_ln", 8);
	struct qstr q_subdir = QSTR_INIT("subdir", 6);
	struct ext4_fc_head head = {
		.fc_features = 0,
		.fc_tid = cpu_to_le32(10),
	};
	u32 crc = 0;
	int snap_err = EXT4_FC_SNAP_ERR_NONE;

	dentries = kunit_kcalloc(test, 5, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	d_dir = &dentries[0];
	d_f1 = &dentries[1];
	d_f1_link = &dentries[2];
	d_subdir = &dentries[3];
	d_f1_overwrite = &dentries[4];

	/* Initialize parent directory on-disk block */
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root, dir), 0);

	init_mock_dentry(d_dir, NULL, "dir", dir, sb);
	init_mock_dentry(d_f1, d_dir, "file1", file1, sb);
	init_mock_dentry(d_f1_link, d_dir, "file1_ln", file1, sb);
	init_mock_dentry(d_subdir, d_dir, "subdir", subdir, sb);

	/* Prepare file1 and subdir snapshots */
	file1->i_generation = 0x4242;
	i_size_write(file1, 2048);
	sync_inode_to_raw(f1_slot);
	sync_inode_to_raw(find_slot(ctx, subdir));
	KUNIT_ASSERT_EQ(test,
			ext4_fc_snapshot_inode_test(file1, 0, NULL, &snap_err),
			0);
	KUNIT_ASSERT_EQ(test,
			ext4_fc_snapshot_inode_test(subdir, 0, NULL, &snap_err),
			0);

	/*
	 * Build a fast commit block containing:
	 *   HEAD -> INODE(file1) -> CREAT(file1) -> LINK(file1_ln) ->
	 *   INODE(subdir) -> CREAT(subdir) -> UNLINK(file1) -> TAIL
	 */
	sbi->s_fc_bytes = 0;
	ctx->journal.j_fc_off = 0;
	ctx->running_txn.t_tid = 10;

	ext4_fc_track_create(&ctx->handle, d_f1);
	ext4_fc_track_link(&ctx->handle, file1, d_f1_link);
	ext4_fc_track_create(&ctx->handle, d_subdir);
	ext4_fc_track_unlink(&ctx->handle, d_f1);

	KUNIT_ASSERT_TRUE(test,
			  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD,
					       sizeof(head), (u8 *)&head, &crc));
	/* Commit all queued dentry updates into block 0 */
	{
		struct ext4_fc_dentry_update *fcd;

		list_for_each_entry(fcd, &sbi->s_fc_dentry_q[FC_Q_MAIN], fcd_list) {
			struct ext4_fc_dentry_info fcd_info = {
				.fc_parent_ino = cpu_to_le32(fcd->fcd_parent),
				.fc_ino = cpu_to_le32(fcd->fcd_ino),
			};
			u8 d_buf[64];
			int dlen = fcd->fcd_name.name.len;

			if (fcd->fcd_op == EXT4_FC_TAG_CREAT) {
				struct inode *c_ino =
					get_mock_inode(ctx, fcd->fcd_ino);
				KUNIT_ASSERT_EQ(test,
						ext4_fc_write_inode_test(c_ino, &crc),
						0);
			}
			memcpy(d_buf, &fcd_info, sizeof(fcd_info));
			memcpy(d_buf + sizeof(fcd_info),
			       fcd->fcd_name.name.name, dlen);
			KUNIT_ASSERT_TRUE(test,
					  ext4_fc_add_tlv_test(sb, fcd->fcd_op,
							       sizeof(fcd_info) + dlen,
							       d_buf, &crc));
		}
	}
	KUNIT_ASSERT_EQ(test, ext4_fc_write_tail_test(sb, crc), 0);

	/* Clear file1 generation in raw_buf to verify replay restores it */
	((struct ext4_inode *)f1_slot->raw_buf)->i_generation = 0;
	file1->i_generation = 0;

	/* Scan and replay block 0 */
	memset(&sbi->s_fc_replay_state, 0, sizeof(sbi->s_fc_replay_state));
	KUNIT_ASSERT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			JBD2_FC_REPLAY_CONTINUE);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_replay_state.fc_replay_num_tags, 8);

	/* Error injection: ext4_get_fc_inode_loc failure aborts INODE replay */
	ctx->fail_get_fc_inode_loc = -EIO;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 0, 10),
			-EIO);
	ctx->fail_get_fc_inode_loc = 0;

	/* Error injection: sync_dirty_buffer failure aborts INODE replay */
	sbi->s_fc_replay_state.fc_replay_num_tags = 8;
	ctx->fail_sync_dirty_buffer = true;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 0, 10),
			-EIO);
	ctx->fail_sync_dirty_buffer = false;

	/* Successful full replay of block 0 */
	sbi->s_fc_replay_state.fc_replay_num_tags = 8;
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 0, 10),
			JBD2_FC_REPLAY_CONTINUE);
	KUNIT_EXPECT_EQ(test, sbi->s_fc_replay_state.fc_replay_num_tags, 0);

	/* Calling PASS_REPLAY once fc_replay_num_tags == 0 runs bitmaps & stops */
	KUNIT_EXPECT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 1, 10),
			0);

	/* Verify replayed state: file1 generation restored, file1 unlinked, file1_ln & subdir present */
	KUNIT_EXPECT_EQ(test, file1->i_generation, 0x4242U);

	bh = ext4_find_entry_test(dir, &q_f1, &de, NULL);
	KUNIT_EXPECT_NULL(test, bh);

	bh = ext4_find_entry_test(dir, &q_f1_ln, &de, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_FILE1_INO);
	brelse(bh);

	bh = ext4_find_entry_test(dir, &q_subdir, &de, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_SUBDIR_INO);
	brelse(bh);

	/*
	 * Exercise additional replay branches:
	 *   - LINK on an already-existing entry ("file1_ln") ignores -EEXIST
	 *   - UNLINK on an already-removed entry ("file1") returns 0
	 *   - UNLINK when dentry points to a different inode (simulating
	 *     rename-overwrite) leaves the dentry intact
	 *   - INODE replay of an inline-data inode copies raw_fc_inode->i_block
	 */
	{
		struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
		struct fc_mock_inode_slot *f2_slot = find_slot(ctx, file2);
		struct ext4_inode *raw_f2 = (struct ext4_inode *)f2_slot->raw_buf;
		struct ext4_fc_dentry_info fcd_info;
		u8 d_buf[64];

		/* Prepare inline-data file2 snapshot */
		ext4_clear_inode_flag(file2, EXT4_INODE_EXTENTS);
		ext4_set_inode_flag(file2, EXT4_INODE_INLINE_DATA);
		sync_inode_to_raw(f2_slot);
		memcpy(raw_f2->i_block, "inline-test-payload", 20);
		KUNIT_ASSERT_EQ(test,
				ext4_fc_snapshot_inode_test(file2, 0, NULL,
							    &snap_err),
				0);
		memset(raw_f2->i_block, 0, sizeof(raw_f2->i_block));

		/* Build block: HEAD -> LINK(file1_ln) -> UNLINK(file1) -> INODE(file2) -> TAIL */
		sbi->s_fc_bytes = 0;
		ctx->journal.j_fc_off = 0;
		crc = 0;
		KUNIT_ASSERT_TRUE(test,
				  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_HEAD,
						       sizeof(head),
						       (u8 *)&head, &crc));

		/* LINK("file1_ln") when "file1_ln" already exists */
		fcd_info.fc_parent_ino = cpu_to_le32(TEST_DIR_INO);
		fcd_info.fc_ino = cpu_to_le32(TEST_FILE1_INO);
		memcpy(d_buf, &fcd_info, sizeof(fcd_info));
		memcpy(d_buf + sizeof(fcd_info), "file1_ln", 8);
		KUNIT_ASSERT_TRUE(test,
				  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_LINK,
						       sizeof(fcd_info) + 8,
						       d_buf, &crc));

		/* UNLINK("file1", ino=TEST_FILE1_INO) */
		memcpy(d_buf, &fcd_info, sizeof(fcd_info));
		memcpy(d_buf + sizeof(fcd_info), "file1", 5);
		KUNIT_ASSERT_TRUE(test,
				  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_UNLINK,
						       sizeof(fcd_info) + 5,
						       d_buf, &crc));

		/* INODE(file2) with inline data */
		KUNIT_ASSERT_EQ(test, ext4_fc_write_inode_test(file2, &crc), 0);
		KUNIT_ASSERT_EQ(test, ext4_fc_write_tail_test(sb, crc), 0);

		/* 1) Replay when "file1" does not exist (UNLINK no-op) */
		sbi->s_fc_replay_state.fc_replay_num_tags = 5;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				JBD2_FC_REPLAY_CONTINUE);
		KUNIT_EXPECT_EQ(test,
				memcmp(raw_f2->i_block, "inline-test-payload", 20),
				0);

		/* 2) Add "file1" pointing to file2, then replay UNLINK(file1, ino=file1) */
		init_mock_dentry(d_f1_overwrite, d_dir, "file1", file2, sb);
		KUNIT_ASSERT_EQ(test,
				__ext4_link(dir, file2, &d_f1_overwrite->d_name,
					    d_f1_overwrite),
				0);

		sbi->s_fc_replay_state.fc_replay_num_tags = 5;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				JBD2_FC_REPLAY_CONTINUE);

		/* Verify "file1" still points to file2 after UNLINK(file1, ino=file1) */
		bh = ext4_find_entry_test(dir, &q_f1, &de, NULL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
		KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode),
				(u32)TEST_FILE2_INO);
		brelse(bh);
	}

	ext4_fc_replay_cleanup(sb);
}

/* =========================================================================
 * 11. Replay of Extent Ranges and End-to-End Commit-to-Replay Round-Trip
 * =========================================================================
 */

static void test_fc_replay_ranges_and_end_to_end(struct kunit *test)
{
	struct fc_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_sb_info *sbi = ctx->sbi;
	struct inode *root = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct ext4_inode_info *ei1 = EXT4_I(file1);
	struct fc_mock_inode_slot *f1_slot = find_slot(ctx, file1);
	struct dentry d_dir, d_f1;
	struct ext4_map_blocks map;
	struct ext4_dir_entry_2 *de = NULL;
	struct buffer_head *bh;
	struct qstr q_f1 = QSTR_INIT("e2e_file", 8);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root, dir), 0);
	init_mock_dentry(&d_dir, NULL, "dir", dir, sb);
	init_mock_dentry(&d_f1, &d_dir, "e2e_file", file1, sb);

	/*
	 * Set up file1 modifications for a real ext4_fc_commit():
	 *   - [0, 3]: written extent at pblk 300
	 *   - [4, 5]: hole (DEL_RANGE)
	 *   - [6, 9]: unwritten extent at pblk 310
	 */
	ctx->journal.j_commit_sequence = 9;
	ctx->running_txn.t_tid = 10;
	ctx->journal.j_fc_off = 0;
	sbi->s_fc_bytes = 0;

	i_size_write(file1, 10 * TEST_BLK_SIZE);
	file1->i_generation = 0x7777;
	sync_inode_to_raw(f1_slot);

	down_write(&ei1->i_data_sem);
	ext4_es_insert_extent(file1, 0, 4, 300, EXTENT_STATUS_WRITTEN, false);
	ext4_es_insert_extent(file1, 4, 2, 0, EXTENT_STATUS_HOLE, false);
	ext4_es_insert_extent(file1, 6, 4, 310, EXTENT_STATUS_UNWRITTEN, false);
	up_write(&ei1->i_data_sem);

	ext4_fc_track_create(&ctx->handle, &d_f1);
	ext4_fc_track_range(&ctx->handle, file1, 0, 9);

	/* Execute real ext4_fc_commit() */
	KUNIT_ASSERT_EQ(test, ext4_fc_commit(&ctx->journal, 10), 0);
	KUNIT_EXPECT_EQ(test, ctx->journal.j_fc_off, 1U);

	/*
	 * Simulate pre-crash on-disk state before replay:
	 *   - [0, 3] was mapped to old pblk 250 (to exercise pblk remap &
	 *     old block bitmap freeing in ext4_fc_replay_add_range)
	 *   - [4, 5] was mapped to pblk 260 (to exercise DEL_RANGE removal)
	 *   - [6, 9] was mapped to pblk 310 as written (to exercise state toggle)
	 */
	ext4_ext_tree_init(NULL, file1);
	{
		struct ext4_ext_path *path;
		struct ext4_extent ex;

		memset(&ex, 0, sizeof(ex));
		ex.ee_block = cpu_to_le32(0);
		ex.ee_len = cpu_to_le16(4);
		ext4_ext_store_pblock(&ex, 250);
		path = ext4_find_extent(file1, 0, NULL, 0);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		down_write(&ei1->i_data_sem);
		path = ext4_ext_insert_extent(NULL, file1, path, &ex, 0);
		up_write(&ei1->i_data_sem);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		ext4_free_ext_path(path);

		memset(&ex, 0, sizeof(ex));
		ex.ee_block = cpu_to_le32(4);
		ex.ee_len = cpu_to_le16(2);
		ext4_ext_store_pblock(&ex, 260);
		path = ext4_find_extent(file1, 4, NULL, 0);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		down_write(&ei1->i_data_sem);
		path = ext4_ext_insert_extent(NULL, file1, path, &ex, 0);
		up_write(&ei1->i_data_sem);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		ext4_free_ext_path(path);

		memset(&ex, 0, sizeof(ex));
		ex.ee_block = cpu_to_le32(6);
		ex.ee_len = cpu_to_le16(4);
		ext4_ext_store_pblock(&ex, 310);
		path = ext4_find_extent(file1, 6, NULL, 0);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		down_write(&ei1->i_data_sem);
		path = ext4_ext_insert_extent(NULL, file1, path, &ex, 0);
		up_write(&ei1->i_data_sem);
		KUNIT_ASSERT_FALSE(test, IS_ERR(path));
		ext4_free_ext_path(path);
	}

	/* Run PASS_SCAN followed by PASS_REPLAY on the committed block */
	ctx->nr_bb_marks = 0;
	memset(&sbi->s_fc_replay_state, 0, sizeof(sbi->s_fc_replay_state));
	KUNIT_ASSERT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_SCAN, 0, 10),
			JBD2_FC_REPLAY_CONTINUE);
	KUNIT_EXPECT_GT(test, sbi->s_fc_replay_state.fc_replay_num_tags, 0);
	/* Both ADD_RANGE extents ([0,3]->300 and [6,9]->310) are excluded */
	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 300));
	KUNIT_EXPECT_TRUE(test, ext4_fc_replay_check_excluded(sb, 312));

	KUNIT_ASSERT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 0, 10),
			JBD2_FC_REPLAY_CONTINUE);
	KUNIT_ASSERT_EQ(test,
			ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
					    PASS_REPLAY, 1, 10),
			0);

	/* 1) Verify directory entry "e2e_file" was created in dir */
	bh = ext4_find_entry_test(dir, &q_f1, &de, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), (u32)TEST_FILE1_INO);
	brelse(bh);

	/* 2) Verify [0, 3] is mapped to new pblk 300 (written) */
	map.m_lblk = 0;
	map.m_len = 4;
	KUNIT_EXPECT_EQ(test, ext4_ext_map_blocks(NULL, file1, &map, 0), 4);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 300ULL);
	KUNIT_EXPECT_TRUE(test, (map.m_flags & EXT4_MAP_MAPPED) != 0);

	/* 3) Verify [4, 5] hole was punched by DEL_RANGE */
	map.m_lblk = 4;
	map.m_len = 2;
	KUNIT_EXPECT_EQ(test, ext4_ext_map_blocks(NULL, file1, &map, 0), 0);

	/* 4) Verify [6, 9] is mapped to pblk 310 as unwritten */
	map.m_lblk = 6;
	map.m_len = 4;
	KUNIT_EXPECT_EQ(test, ext4_ext_map_blocks(NULL, file1, &map, 0), 4);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 310ULL);
	KUNIT_EXPECT_TRUE(test, (map.m_flags & EXT4_MAP_UNWRITTEN) != 0);

	/* 5) Verify ext4_mb_mark_bb was invoked to free old blocks & mark new ones */
	KUNIT_EXPECT_GT(test, ctx->nr_bb_marks, 0U);

	/*
	 * 6) Error handling in ADD_RANGE and DEL_RANGE replay:
	 *    - DEL_RANGE when ext4_map_blocks fails propagates error
	 *    - ADD_RANGE when ext4_map_blocks, ext4_find_extent, or
	 *      ext4_ext_insert_extent fails propagates error
	 *    - ADD_RANGE / DEL_RANGE when ext4_iget fails returns 0
	 */
	{
		struct ext4_fc_del_range del_arg;
		struct ext4_fc_add_range add_arg;
		struct ext4_extent *ex = (struct ext4_extent *)add_arg.fc_ex;
		u32 crc = 0;

		/* DEL_RANGE: ext4_iget failure returns 0; ext4_map_blocks failure returns -EIO */
		sbi->s_fc_bh = NULL;
		sbi->s_fc_bytes = 0;
		ctx->journal.j_fc_off = 0;
		del_arg.fc_ino = cpu_to_le32(TEST_FILE1_INO);
		del_arg.fc_lblk = cpu_to_le32(0);
		del_arg.fc_len = cpu_to_le32(2);
		KUNIT_ASSERT_TRUE(test,
				  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_DEL_RANGE,
						       sizeof(del_arg),
						       (u8 *)&del_arg, &crc));

		sbi->s_fc_replay_state.fc_replay_num_tags = 1;
		ctx->fail_iget = -ENOENT;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				0);
		ctx->fail_iget = 0;

		sbi->s_fc_replay_state.fc_replay_num_tags = 1;
		ctx->fail_map_blocks = -EIO;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				-EIO);
		ctx->fail_map_blocks = 0;

		/* ADD_RANGE on unmapped range [20, 21]: test map_blocks, find_extent, insert_extent failures */
		sbi->s_fc_bh = NULL;
		sbi->s_fc_bytes = 0;
		ctx->journal.j_fc_off = 0;
		memset(&add_arg, 0, sizeof(add_arg));
		add_arg.fc_ino = cpu_to_le32(TEST_FILE1_INO);
		ex->ee_block = cpu_to_le32(20);
		ex->ee_len = cpu_to_le16(2);
		ext4_ext_store_pblock(ex, 400);
		KUNIT_ASSERT_TRUE(test,
				  ext4_fc_add_tlv_test(sb, EXT4_FC_TAG_ADD_RANGE,
						       sizeof(add_arg),
						       (u8 *)&add_arg, &crc));

		sbi->s_fc_replay_state.fc_replay_num_tags = 1;
		ctx->fail_map_blocks = -EIO;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				-EIO);
		ctx->fail_map_blocks = 0;

		sbi->s_fc_replay_state.fc_replay_num_tags = 1;
		ctx->fail_find_extent = -EIO;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				-EIO);
		ctx->fail_find_extent = 0;

		sbi->s_fc_replay_state.fc_replay_num_tags = 1;
		ctx->fail_insert_extent = -ENOSPC;
		KUNIT_EXPECT_EQ(test,
				ext4_fc_replay_test(&ctx->journal, &ctx->fc_bhs[0],
						    PASS_REPLAY, 0, 10),
				-ENOSPC);
		ctx->fail_insert_extent = 0;
	}

	ext4_fc_replay_cleanup(sb);
	KUNIT_EXPECT_FALSE(test, (sbi->s_mount_state & EXT4_FC_REPLAY) != 0);
}

static struct kunit_case fast_commit_test_cases[] = {
	KUNIT_CASE(test_fc_init_and_eligibility),
	KUNIT_CASE(test_fc_mark_ineligible),
	KUNIT_CASE(test_fc_track_inode_and_range),
	KUNIT_CASE(test_fc_track_dentry_and_del),
	KUNIT_CASE(test_fc_reserve_space_and_tlv_writing),
	KUNIT_CASE(test_fc_snapshot_inode_and_ranges),
	KUNIT_CASE(test_fc_cleanup_and_stats),
	KUNIT_CASE(test_fc_commit_paths),
	KUNIT_CASE(test_fc_replay_regions_and_scan),
	KUNIT_CASE(test_fc_replay_inode_and_dentries),
	KUNIT_CASE(test_fc_replay_ranges_and_end_to_end),
	{}
};

static struct kunit_suite fast_commit_test_suite = {
	.name = "ext4_fast_commit_test",
	.init = fast_commit_test_init,
	.test_cases = fast_commit_test_cases,
};

kunit_test_suites(&fast_commit_test_suite);

MODULE_LICENSE("GPL");
