// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 directory lookup, HTree indexing, and namespace
 * operations (fs/ext4/namei.c).
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/iversion.h>
#include <linux/quotaops.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/user_namespace.h>
#include <linux/unicode.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "namei.h"

#define TEST_BLK_SIZE		1024
#define TEST_BLK_BITS		10
#define TEST_INODE_SIZE		256
#define TEST_EXTRA_ISIZE	(sizeof(struct ext4_inode) - \
				 EXT4_GOOD_OLD_INODE_SIZE)
#define TEST_FIRST_PBLK		100
#define TEST_MAX_BLOCKS		64
#define TEST_MAX_LBLOCKS	16
#define TEST_MAX_INODES		8

#define TEST_ROOT_INO		EXT4_ROOT_INO
#define TEST_DIR_INO		12
#define TEST_DIR2_INO		13
#define TEST_SUBDIR_INO		14
#define TEST_FILE1_INO		15
#define TEST_FILE2_INO		16
#define TEST_FILE3_INO		17
#define TEST_CSUM_SEED		0x12345678U

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
	ext4_fsblk_t lblk_to_pblk[TEST_MAX_LBLOCKS];
};

struct namei_test_ctx {
	struct super_block sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct block_device bdev;
	struct address_space bd_mapping;
	struct buffer_head sb_bh;

	struct mock_inode_slot inodes[TEST_MAX_INODES];
	struct mock_block blocks[TEST_MAX_BLOCKS];

	int fail_map_blocks;
	int force_map_blocks_ret;
	int fail_getblk;
	int fail_get_inode_loc;
	int fail_mark_iloc_dirty;
	int fail_dirty_metadata;
	int fail_iget;

	unsigned long last_iget_ino;
	unsigned int orphan_add_calls;
	unsigned long last_orphan_add_ino;
	unsigned int orphan_del_calls;
	unsigned long last_orphan_del_ino;
	unsigned int dirty_metadata_calls;
	unsigned int mark_inode_dirty_calls;
};

#ifdef CONFIG_QUOTA
static struct dquot __rcu **namei_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *namei_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations namei_test_qops = {
	.get_reserved_space = namei_test_get_reserved_space,
};
#endif

static const struct super_operations namei_test_sops = {
#ifdef CONFIG_QUOTA
	.get_dquots	= namei_test_get_dquots,
#endif
};

static void init_mock_inode_slot(struct namei_test_ctx *ctx,
				 struct mock_inode_slot *slot,
				 unsigned long ino, umode_t mode)
{
	struct inode *inode = &slot->ei.vfs_inode;
	struct ext4_inode *raw_inode;

	memset(slot, 0, sizeof(*slot));
	slot->allocated = true;

	inode->i_sb = &ctx->sb;
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = TEST_BLK_BITS;
	inode->i_mapping = &inode->i_data;
	set_nlink(inode, S_ISDIR(mode) ? 2 : 1);
	atomic_set(&inode->i_count, 1);

	spin_lock_init(&inode->i_lock);
	init_rwsem(&inode->i_rwsem);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	INIT_HLIST_NODE(&inode->i_hash);
	/*
	 * Pre-set all I_DIRTY flags so mark_inode_dirty() is a fastpath
	 * no-op that does not touch superblock/bdi writeback lists.
	 */
	inode_state_set_raw(inode, I_DIRTY);

	init_rwsem(&slot->ei.xattr_sem);
	init_rwsem(&slot->ei.i_data_sem);
	INIT_LIST_HEAD(&slot->ei.i_orphan);
	INIT_LIST_HEAD(&slot->ei.i_fc_list);
	spin_lock_init(&slot->ei.i_fc_lock);
	spin_lock_init(&slot->ei.i_raw_lock);

	slot->ei.i_extra_isize = TEST_EXTRA_ISIZE;
	slot->ei.i_csum_seed = TEST_CSUM_SEED ^ (u32)ino;
	slot->ei.i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	slot->iloc_bh.b_data = slot->raw_buf;
	slot->iloc_bh.b_size = TEST_INODE_SIZE;
	slot->iloc_bh.b_blocknr = 10 + ino;
	atomic_set(&slot->iloc_bh.b_count, 1);
	set_buffer_uptodate(&slot->iloc_bh);

	raw_inode = (struct ext4_inode *)slot->raw_buf;
	raw_inode->i_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);
}

static struct mock_inode_slot *find_inode_slot_by_ino(struct namei_test_ctx *ctx,
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

static struct mock_inode_slot *find_inode_slot(struct namei_test_ctx *ctx,
					       struct inode *inode)
{
	return find_inode_slot_by_ino(ctx, inode->i_ino);
}

static struct inode *get_mock_inode(struct namei_test_ctx *ctx,
				    unsigned long ino)
{
	struct mock_inode_slot *slot = find_inode_slot_by_ino(ctx, ino);

	return slot ? &slot->ei.vfs_inode : NULL;
}

static struct mock_block *get_mock_block(struct namei_test_ctx *ctx,
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

static ext4_fsblk_t alloc_mock_pblk(struct namei_test_ctx *ctx)
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

static struct buffer_head *map_and_get_dir_bh(struct namei_test_ctx *ctx,
					      struct inode *inode,
					      ext4_lblk_t lblk)
{
	struct mock_inode_slot *slot = find_inode_slot(ctx, inode);
	struct mock_block *blk;
	ext4_fsblk_t pblk;

	if (!slot || lblk >= TEST_MAX_LBLOCKS)
		return NULL;
	pblk = slot->lblk_to_pblk[lblk];
	if (!pblk) {
		pblk = alloc_mock_pblk(ctx);
		if (!pblk)
			return NULL;
		slot->lblk_to_pblk[lblk] = pblk;
	}
	blk = get_mock_block(ctx, pblk);
	return blk ? &blk->bh : NULL;
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
	dentry->__d_name.name = (const unsigned char *)name;
	dentry->__d_name.len = strlen(name);
	if (inode) {
		dentry->d_inode = inode;
		dentry->d_flags = S_ISDIR(inode->i_mode) ?
				  DCACHE_DIRECTORY_TYPE : DCACHE_REGULAR_TYPE;
	} else {
		dentry->d_flags = DCACHE_MISS_TYPE;
	}
}

/* --- KUnit Static Stubs --- */

static int ext4_map_blocks_stub(handle_t *handle, struct inode *inode,
				struct ext4_map_blocks *map, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;
	ext4_fsblk_t pblk;

	if (ctx->fail_map_blocks)
		return ctx->fail_map_blocks;
	if (ctx->force_map_blocks_ret)
		return ctx->force_map_blocks_ret;

	slot = find_inode_slot(ctx, inode);
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

static struct buffer_head *ext4_getblk_stub(handle_t *handle,
					    struct inode *inode,
					    ext4_lblk_t block, int map_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;
	struct mock_block *blk;
	ext4_fsblk_t pblk;

	if (ctx->fail_getblk)
		return ERR_PTR(ctx->fail_getblk);

	slot = find_inode_slot(ctx, inode);
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

static int ext4_get_inode_loc_stub(struct inode *inode, struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;
	struct mock_inode_slot *slot;

	if (ctx->fail_get_inode_loc)
		return ctx->fail_get_inode_loc;

	slot = find_inode_slot(ctx, inode);
	if (!slot)
		return -ENOENT;

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
	struct namei_test_ctx *ctx = test->priv;

	brelse(iloc->bh);
	iloc->bh = NULL;
	if (ctx->fail_mark_iloc_dirty)
		return ctx->fail_mark_iloc_dirty;
	return 0;
}

static int ext4_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
				      const char *func, unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;

	ctx->mark_inode_dirty_calls++;
	if (ctx->fail_mark_iloc_dirty)
		return ctx->fail_mark_iloc_dirty;
	return 0;
}

static int ext4_handle_dirty_metadata_stub(const char *where, unsigned int line,
					   handle_t *handle,
					   struct inode *inode,
					   struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;

	ctx->dirty_metadata_calls++;
	if (ctx->fail_dirty_metadata)
		return ctx->fail_dirty_metadata;
	return 0;
}

static struct inode *ext4_iget_stub(struct super_block *sb, unsigned long ino,
				    ext4_iget_flags flags,
				    const char *function, unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;

	ctx->last_iget_ino = ino;
	if (ctx->fail_iget)
		return ERR_PTR(ctx->fail_iget);
	return ERR_PTR(-ENOENT);
}

static int ext4_orphan_add_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;

	ctx->orphan_add_calls++;
	ctx->last_orphan_add_ino = inode->i_ino;
	if (list_empty(&EXT4_I(inode)->i_orphan))
		list_add(&EXT4_I(inode)->i_orphan, &ctx->sbi.s_orphan);
	return 0;
}

static int ext4_orphan_del_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct namei_test_ctx *ctx = test->priv;

	ctx->orphan_del_calls++;
	ctx->last_orphan_del_ino = inode->i_ino;
	list_del_init(&EXT4_I(inode)->i_orphan);
	return 0;
}

static int namei_test_init(struct kunit *test)
{
	struct namei_test_ctx *ctx;
	int i;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	test->priv = ctx;

	ctx->sb.s_blocksize = TEST_BLK_SIZE;
	ctx->sb.s_blocksize_bits = TEST_BLK_BITS;
	ctx->sb.s_magic = EXT4_SUPER_MAGIC;
	ctx->sb.s_maxbytes = MAX_LFS_FILESIZE;
	ctx->sb.s_time_gran = 1;
	ctx->sb.s_op = &namei_test_sops;
	ctx->sb.s_fs_info = &ctx->sbi;
	ctx->sb.s_user_ns = &init_user_ns;
	ctx->sb.s_bdev = &ctx->bdev;
#ifdef CONFIG_FS_ENCRYPTION
	ctx->sb.s_cop = &ext4_cryptops;
#endif
#ifdef CONFIG_QUOTA
	ctx->sb.dq_op = &namei_test_qops;
#endif
	ctx->bdev.bd_mapping = &ctx->bd_mapping;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	init_rwsem(&ctx->sb.s_umount);
	strscpy(ctx->sb.s_id, "ext4_namei_kunit", sizeof(ctx->sb.s_id));

	ctx->sbi.s_sb = &ctx->sb;
	ctx->sbi.s_es = &ctx->es;
	ctx->sbi.s_sbh = &ctx->sb_bh;
	ctx->sbi.s_inode_size = TEST_INODE_SIZE;
	ctx->sbi.s_first_ino = EXT4_GOOD_OLD_FIRST_INO;
	ctx->sbi.s_want_extra_isize = TEST_EXTRA_ISIZE;
	ctx->sbi.s_def_hash_version = DX_HASH_HALF_MD4;
	ctx->sbi.s_hash_unsigned = 3;
	ctx->sbi.s_hash_seed[0] = 0x11223344U;
	ctx->sbi.s_hash_seed[1] = 0x55667788U;
	ctx->sbi.s_hash_seed[2] = 0x99aabbccU;
	ctx->sbi.s_hash_seed[3] = 0xddeeff00U;
	ctx->sbi.s_csum_seed = TEST_CSUM_SEED;
	set_opt(&ctx->sb, ERRORS_CONT);
	spin_lock_init(&ctx->sbi.s_bdev_wb_lock);
	spin_lock_init(&ctx->sbi.s_error_lock);
	spin_lock_init(&ctx->sbi.s_md_lock);
	mutex_init(&ctx->sbi.s_orphan_lock);
	INIT_LIST_HEAD(&ctx->sbi.s_orphan);
	ratelimit_state_init(&ctx->sbi.s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&ctx->sbi.s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&ctx->sbi.s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	ctx->es.s_inodes_count = cpu_to_le32(1024);
	ctx->es.s_first_ino = cpu_to_le32(EXT4_GOOD_OLD_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(TEST_EXTRA_ISIZE);
	ctx->es.s_feature_incompat = cpu_to_le32(EXT4_FEATURE_INCOMPAT_FILETYPE);
	ctx->es.s_feature_compat = cpu_to_le32(EXT4_FEATURE_COMPAT_DIR_INDEX);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = TEST_BLK_SIZE;
	ctx->sb_bh.b_blocknr = 1;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);

	for (i = 0; i < TEST_MAX_BLOCKS; i++) {
		struct buffer_head *bh = &ctx->blocks[i].bh;

		bh->b_data = ctx->blocks[i].data;
		bh->b_size = TEST_BLK_SIZE;
		bh->b_blocknr = TEST_FIRST_PBLK + i;
		atomic_set(&bh->b_count, 1);
		set_buffer_uptodate(bh);
	}

	init_mock_inode_slot(ctx, &ctx->inodes[0], TEST_ROOT_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[1], TEST_DIR_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[2], TEST_DIR2_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[3], TEST_SUBDIR_INO,
			     S_IFDIR | 0755);
	init_mock_inode_slot(ctx, &ctx->inodes[4], TEST_FILE1_INO,
			     S_IFREG | 0644);
	init_mock_inode_slot(ctx, &ctx->inodes[5], TEST_FILE2_INO,
			     S_IFREG | 0644);
	init_mock_inode_slot(ctx, &ctx->inodes[6], TEST_FILE3_INO,
			     S_IFLNK | 0777);

	kunit_activate_static_stub(test, ext4_map_blocks, ext4_map_blocks_stub);
	kunit_activate_static_stub(test, ext4_getblk, ext4_getblk_stub);
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   ext4_get_inode_loc_stub);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   ext4_mark_iloc_dirty_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   ext4_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   ext4_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_iget, ext4_iget_stub);
	kunit_activate_static_stub(test, ext4_orphan_add, ext4_orphan_add_stub);
	kunit_activate_static_stub(test, ext4_orphan_del, ext4_orphan_del_stub);

	return 0;
}

static void namei_test_exit(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;

	if (!ctx)
		return;
#if IS_ENABLED(CONFIG_UNICODE)
	utf8_unload(ctx->sb.s_encoding);
	ctx->sb.s_encoding = NULL;
#endif
}

/*
 * Test 1: Directory leaf tail initialization, checksum computation, and
 * verification (ext4_initialize_dirent_tail, ext4_dirblock_csum_verify,
 * ext4_handle_dirty_dirblock).
 */
static void test_namei_dirent_tail_and_csum(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct buffer_head *bh;
	struct ext4_dir_entry_tail *t;
	int err;

	bh = map_and_get_dir_bh(ctx, dir, 0);
	KUNIT_ASSERT_NOT_NULL(test, bh);

	/* Without METADATA_CSUM, verification always succeeds */
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);

	/* Enable METADATA_CSUM */
	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	/* Uninitialized tail fails verification */
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 0);

	/* Initialize directory block and tail, then compute csum */
	err = ext4_init_dirblock(NULL, dir, bh, TEST_ROOT_INO, NULL, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	t = EXT4_DIRENT_TAIL(bh->b_data, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(t->det_reserved_zero1), 0U);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(t->det_rec_len),
			sizeof(struct ext4_dir_entry_tail));
	KUNIT_EXPECT_EQ(test, t->det_reserved_zero2, 0);
	KUNIT_EXPECT_EQ(test, t->det_reserved_ft, EXT4_FT_DIR_CSUM);
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);

	/* Flip a bit in the directory data -> checksum mismatch */
	bh->b_data[0] ^= 0x01;
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 0);
	bh->b_data[0] ^= 0x01;
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);

	/* Corrupt tail reserved_ft -> get_dirent_tail() rejects it */
	t->det_reserved_ft = EXT4_FT_REG_FILE;
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 0);
	ext4_initialize_dirent_tail(bh, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh), 0);
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);
}

/*
 * Test 2: HTree dx_entry accessors, dx_root_limit / dx_node_limit, and
 * dx_root / dx_node metadata checksum verification (ext4_dx_csum_set_test,
 * ext4_dx_csum_verify_test).
 */
static void test_namei_dx_helpers_and_csum(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct buffer_head *root_bh, *node_bh;
	struct dx_root *root;
	struct dx_node *node;
	struct dx_entry *entries;

	root_bh = map_and_get_dir_bh(ctx, dir, 0);
	node_bh = map_and_get_dir_bh(ctx, dir, 1);
	KUNIT_ASSERT_NOT_NULL(test, root_bh);
	KUNIT_ASSERT_NOT_NULL(test, node_bh);

	/* Check limits without METADATA_CSUM (1024B block) */
	/* Root: (1024 - 12 - 12 - 8) / 8 = 124 */
	KUNIT_EXPECT_EQ(test,
			dx_root_limit(dir, sizeof(struct dx_root_info)), 124U);
	/* Node: (1024 - 8) / 8 = 127 */
	KUNIT_EXPECT_EQ(test, dx_node_limit(dir), 127U);

	/* Enable METADATA_CSUM -> 1 fewer entry for 8-byte dx_tail */
	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	KUNIT_EXPECT_EQ(test,
			dx_root_limit(dir, sizeof(struct dx_root_info)), 123U);
	KUNIT_EXPECT_EQ(test, dx_node_limit(dir), 126U);

	/* Set up a valid dx_root in root_bh */
	root = (struct dx_root *)root_bh->b_data;
	root->dot.inode = cpu_to_le32(dir->i_ino);
	root->dot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
	root->dot.name_len = 1;
	root->dot.file_type = EXT4_FT_DIR;
	strscpy(root->dot_name, ".", sizeof(root->dot_name));

	root->dotdot.inode = cpu_to_le32(TEST_ROOT_INO);
	root->dotdot.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE - 12,
						    TEST_BLK_SIZE);
	root->dotdot.name_len = 2;
	root->dotdot.file_type = EXT4_FT_DIR;
	strscpy(root->dotdot_name, "..", sizeof(root->dotdot_name));

	root->info.info_length = sizeof(struct dx_root_info);
	root->info.hash_version = DX_HASH_HALF_MD4;
	entries = root->entries;
	dx_set_limit(entries, dx_root_limit(dir, sizeof(struct dx_root_info)));
	dx_set_count(entries, 2);
	dx_set_block(entries + 0, 0xf0000001U); /* high 4 bits masked */
	dx_set_hash(entries + 1, 0x80001234U);
	dx_set_block(entries + 1, 2);

	KUNIT_EXPECT_EQ(test, dx_get_limit(entries), 123U);
	KUNIT_EXPECT_EQ(test, dx_get_count(entries), 2U);
	KUNIT_EXPECT_EQ(test, dx_get_block(entries + 0), 1U);
	KUNIT_EXPECT_EQ(test, dx_get_hash(entries + 1), 0x80001234U);
	KUNIT_EXPECT_EQ(test, dx_get_block(entries + 1), 2U);

	ext4_dx_csum_set_test(dir, (struct ext4_dir_entry *)root_bh->b_data);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)root_bh->b_data), 1);

	/* Corrupt hash -> csum verify fails */
	dx_set_hash(entries + 1, 0x80001235U);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)root_bh->b_data), 0);
	dx_set_hash(entries + 1, 0x80001234U);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)root_bh->b_data), 1);

	/* Overflow limit -> no space for csum fails */
	dx_set_limit(entries, 200);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)root_bh->b_data), 0);

	/* Set up a valid dx_node in node_bh */
	node = (struct dx_node *)node_bh->b_data;
	node->fake.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE, TEST_BLK_SIZE);
	dx_set_limit(node->entries, dx_node_limit(dir));
	dx_set_count(node->entries, 2);
	dx_set_block(node->entries + 0, 3);
	dx_set_hash(node->entries + 1, 0x40000000U);
	dx_set_block(node->entries + 1, 4);

	ext4_dx_csum_set_test(dir, (struct ext4_dir_entry *)node_bh->b_data);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)node_bh->b_data), 1);

	/* Corrupt fake.rec_len -> get_dx_countlimit returns NULL */
	node->fake.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test,
			ext4_dx_csum_verify_test(dir,
				(struct ext4_dir_entry *)node_bh->b_data), 0);
}

/*
 * Test 3: Parameterized tests for __ext4_read_dirblock (ext4_read_dirblock_test).
 */
enum read_dirblock_case_id {
	RDB_VALID_LEAF,
	RDB_VALID_INDEX,
	RDB_HOLE_ALLOWED_EITHER,
	RDB_PAST_ISIZE,
	RDB_BREAD_EIO,
	RDB_HOLE_AT_BLOCK0,
	RDB_HOLE_FOR_HTREE_LEAF,
	RDB_LEAF_INSTEAD_OF_INDEX,
	RDB_BAD_LEAF_CSUM,
	RDB_BAD_INDEX_CSUM,
};

struct read_dirblock_param {
	const char *desc;
	enum read_dirblock_case_id id;
	ext4_lblk_t block;
	enum dirblock_type type;
	int expected_err;
	bool expect_null;
};

static const struct read_dirblock_param read_dirblock_params[] = {
	{
		.desc = "valid leaf block with metadata_csum",
		.id = RDB_VALID_LEAF,
		.block = 0,
		.type = DIRENT,
		.expected_err = 0,
	},
	{
		.desc = "valid dx_root index block with metadata_csum",
		.id = RDB_VALID_INDEX,
		.block = 0,
		.type = INDEX,
		.expected_err = 0,
	},
	{
		.desc = "hole at block > 0 with EITHER returns NULL",
		.id = RDB_HOLE_ALLOWED_EITHER,
		.block = 1,
		.type = EITHER,
		.expected_err = 0,
		.expect_null = true,
	},
	{
		.desc = "reading block past i_size returns -EFSCORRUPTED",
		.id = RDB_PAST_ISIZE,
		.block = 2,
		.type = EITHER,
		.expected_err = -EFSCORRUPTED,
	},
	{
		.desc = "ext4_bread -EIO error propagates",
		.id = RDB_BREAD_EIO,
		.block = 0,
		.type = DIRENT,
		.expected_err = -EIO,
	},
	{
		.desc = "hole at block 0 returns -EFSCORRUPTED",
		.id = RDB_HOLE_AT_BLOCK0,
		.block = 0,
		.type = EITHER,
		.expected_err = -EFSCORRUPTED,
	},
	{
		.desc = "hole for DIRENT_HTREE returns -EFSCORRUPTED",
		.id = RDB_HOLE_FOR_HTREE_LEAF,
		.block = 1,
		.type = DIRENT_HTREE,
		.expected_err = -EFSCORRUPTED,
	},
	{
		.desc = "leaf block found when expecting INDEX returns -EFSCORRUPTED",
		.id = RDB_LEAF_INSTEAD_OF_INDEX,
		.block = 1,
		.type = INDEX,
		.expected_err = -EFSCORRUPTED,
	},
	{
		.desc = "corrupted leaf checksum returns -EFSBADCRC",
		.id = RDB_BAD_LEAF_CSUM,
		.block = 0,
		.type = DIRENT,
		.expected_err = -EFSBADCRC,
	},
	{
		.desc = "corrupted index checksum returns -EFSBADCRC",
		.id = RDB_BAD_INDEX_CSUM,
		.block = 0,
		.type = INDEX,
		.expected_err = -EFSBADCRC,
	},
};

static void read_dirblock_get_desc(const struct read_dirblock_param *p,
				   char *desc)
{
	strscpy(desc, p->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(read_dirblock, read_dirblock_params, read_dirblock_get_desc);

static void test_namei_read_dirblock(struct kunit *test)
{
	const struct read_dirblock_param *p = test->param_value;
	struct namei_test_ctx *ctx = test->priv;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct buffer_head *bh, *res;
	struct dx_root *root;

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	dir->i_size = 2 * TEST_BLK_SIZE;

	switch (p->id) {
	case RDB_VALID_LEAF:
		bh = map_and_get_dir_bh(ctx, dir, 0);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		KUNIT_ASSERT_EQ(test,
			ext4_init_dirblock(NULL, dir, bh, TEST_ROOT_INO,
					   NULL, 0), 0);
		clear_buffer_verified(bh);
		break;
	case RDB_VALID_INDEX:
	case RDB_BAD_INDEX_CSUM:
		ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
		bh = map_and_get_dir_bh(ctx, dir, 0);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		root = (struct dx_root *)bh->b_data;
		root->dot.inode = cpu_to_le32(dir->i_ino);
		root->dot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dot.name_len = 1;
		strscpy(root->dot_name, ".", sizeof(root->dot_name));
		root->dotdot.inode = cpu_to_le32(TEST_ROOT_INO);
		root->dotdot.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE - 12,
							    TEST_BLK_SIZE);
		root->dotdot.name_len = 2;
		strscpy(root->dotdot_name, "..", sizeof(root->dotdot_name));
		root->info.info_length = sizeof(struct dx_root_info);
		root->info.hash_version = DX_HASH_HALF_MD4;
		dx_set_limit(root->entries,
			     dx_root_limit(dir, sizeof(struct dx_root_info)));
		dx_set_count(root->entries, 1);
		dx_set_block(root->entries, 1);
		ext4_dx_csum_set_test(dir, (struct ext4_dir_entry *)bh->b_data);
		if (p->id == RDB_BAD_INDEX_CSUM)
			dx_set_block(root->entries, 99);
		clear_buffer_verified(bh);
		break;
	case RDB_HOLE_ALLOWED_EITHER:
	case RDB_PAST_ISIZE:
	case RDB_HOLE_AT_BLOCK0:
	case RDB_HOLE_FOR_HTREE_LEAF:
		break;
	case RDB_BREAD_EIO:
		ctx->fail_getblk = -EIO;
		break;
	case RDB_LEAF_INSTEAD_OF_INDEX:
		ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
		bh = map_and_get_dir_bh(ctx, dir, 1);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		KUNIT_ASSERT_EQ(test,
			ext4_init_dirblock(NULL, dir, bh, TEST_ROOT_INO,
					   NULL, 0), 0);
		clear_buffer_verified(bh);
		break;
	case RDB_BAD_LEAF_CSUM:
		bh = map_and_get_dir_bh(ctx, dir, 0);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		KUNIT_ASSERT_EQ(test,
			ext4_init_dirblock(NULL, dir, bh, TEST_ROOT_INO,
					   NULL, 0), 0);
		bh->b_data[0] ^= 0xff;
		clear_buffer_verified(bh);
		break;
	}

	res = ext4_read_dirblock_test(dir, p->block, p->type);
	if (p->expected_err) {
		KUNIT_EXPECT_TRUE(test, IS_ERR(res));
		KUNIT_EXPECT_EQ(test, PTR_ERR(res), p->expected_err);
	} else if (p->expect_null) {
		KUNIT_EXPECT_NULL(test, res);
	} else {
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(res));
		KUNIT_EXPECT_TRUE(test, buffer_verified(res));
		brelse(res);
	}
}

/*
 * Test 4: Directory initialization and block appending (ext4_init_new_dir,
 * ext4_append_test, s_max_dir_size_kb limit, already-allocated block check,
 * and ext4_init_dirblock with inline_buf conversion data).
 */
static void test_namei_append_and_init_dir(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	char inline_entries[64];
	ext4_lblk_t lblk = 0;
	int err;

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	err = ext4_init_new_dir(NULL, root_dir, dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 2U);
	KUNIT_EXPECT_EQ(test, dir->i_size, (loff_t)TEST_BLK_SIZE);

	/* Append a second block (lblk 1) */
	bh = ext4_append_test(NULL, dir, &lblk);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_EXPECT_EQ(test, lblk, 1U);
	KUNIT_EXPECT_EQ(test, dir->i_size, (loff_t)(2 * TEST_BLK_SIZE));
	brelse(bh);

	/* Enforce s_max_dir_size_kb limit -> -ENOSPC */
	ctx->sbi.s_max_dir_size_kb = 2;
	bh = ext4_append_test(NULL, dir, &lblk);
	KUNIT_EXPECT_TRUE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -ENOSPC);
	ctx->sbi.s_max_dir_size_kb = 0;

	/* Pre-allocated logical block at append target -> -EFSCORRUPTED */
	ctx->force_map_blocks_ret = 1;
	bh = ext4_append_test(NULL, dir, &lblk);
	KUNIT_EXPECT_TRUE(test, IS_ERR(bh));
	KUNIT_EXPECT_EQ(test, PTR_ERR(bh), -EFSCORRUPTED);
	ctx->force_map_blocks_ret = 0;

	/* Test ext4_init_dirblock with non-NULL inline_buf */
	memset(inline_entries, 0, sizeof(inline_entries));
	de = (struct ext4_dir_entry_2 *)inline_entries;
	de->inode = cpu_to_le32(TEST_FILE1_INO);
	de->name_len = 3;
	de->file_type = EXT4_FT_REG_FILE;
	memcpy(de->name, "foo", 3);
	de->rec_len = ext4_rec_len_to_disk(sizeof(inline_entries),
					   TEST_BLK_SIZE);

	bh = map_and_get_dir_bh(ctx, dir, 1);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	err = ext4_init_dirblock(NULL, dir, bh, TEST_ROOT_INO,
				 inline_entries, sizeof(inline_entries));
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);

	/* Verify '.', '..', and 'foo' in the initialized block */
	de = (struct ext4_dir_entry_2 *)bh->b_data;
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), dir->i_ino);
	de = ext4_next_entry(de, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_ROOT_INO);
	de = ext4_next_entry(de, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
	KUNIT_EXPECT_EQ(test, de->name_len, 3);
	KUNIT_EXPECT_MEMEQ(test, de->name, "foo", 3);
	KUNIT_EXPECT_EQ(test, ext4_rec_len_from_disk(de->rec_len, TEST_BLK_SIZE),
			TEST_BLK_SIZE - 24 -
			(int)sizeof(struct ext4_dir_entry_tail));
}

/*
 * Test 5: Directory entry destination search, insertion, linear lookup, and
 * deletion (ext4_find_dest_de, ext4_insert_dentry, ext4_search_dir,
 * ext4_delete_entry_test, ext4_generic_delete_entry).
 */
static void test_namei_search_insert_delete_dentry(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de = NULL, *res_de = NULL;
	struct ext4_filename fname1, fname2;
	struct qstr q1 = QSTR_INIT("alpha.txt", 9);
	struct qstr q2 = QSTR_INIT("beta.txt", 8);
	int csum_size = sizeof(struct ext4_dir_entry_tail);
	int buf_size = TEST_BLK_SIZE - csum_size;
	int err;

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	bh = map_and_get_dir_bh(ctx, dir, 0);
	KUNIT_ASSERT_NOT_NULL(test, bh);

	KUNIT_ASSERT_EQ(test, ext4_fname_setup_filename(dir, &q1, 0, &fname1), 0);
	KUNIT_ASSERT_EQ(test, ext4_fname_setup_filename(dir, &q2, 0, &fname2), 0);

	/* Find space and insert alpha.txt */
	err = ext4_find_dest_de(dir, bh, bh->b_data, buf_size, &fname1, &de);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_NOT_NULL(test, de);
	ext4_insert_dentry(dir, file1, de, TEST_BLK_SIZE, &fname1);
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh), 0);

	/* Duplicate insert of alpha.txt must return -EEXIST */
	err = ext4_find_dest_de(dir, bh, bh->b_data, buf_size, &fname1, &de);
	KUNIT_EXPECT_EQ(test, err, -EEXIST);

	/* Insert beta.txt */
	err = ext4_find_dest_de(dir, bh, bh->b_data, buf_size, &fname2, &de);
	KUNIT_ASSERT_EQ(test, err, 0);
	ext4_insert_dentry(dir, file2, de, TEST_BLK_SIZE, &fname2);
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh), 0);

	/* Search for both entries via ext4_search_dir */
	err = ext4_search_dir(bh, bh->b_data, buf_size, dir, &fname1, 0,
			      &res_de);
	KUNIT_ASSERT_EQ(test, err, 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(res_de->inode), TEST_FILE1_INO);
	KUNIT_EXPECT_EQ(test, res_de->file_type, EXT4_FT_REG_FILE);

	/* Delete alpha.txt via ext4_delete_entry_test (merges into '..') */
	err = ext4_delete_entry_test(NULL, dir, res_de, bh);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ext4_dirblock_csum_verify(dir, bh), 1);

	/* alpha.txt is gone, beta.txt is still present */
	KUNIT_EXPECT_EQ(test,
		ext4_search_dir(bh, bh->b_data, buf_size, dir, &fname1, 0,
				&res_de), 0);
	KUNIT_EXPECT_EQ(test,
		ext4_search_dir(bh, bh->b_data, buf_size, dir, &fname2, 0,
				&res_de), 1);

	/*
	 * Test deleting the very first entry in a buffer (pde == NULL),
	 * which zeroes de->inode while preserving rec_len.
	 */
	de = (struct ext4_dir_entry_2 *)bh->b_data;
	err = ext4_generic_delete_entry(dir, de, bh, bh->b_data,
					TEST_BLK_SIZE, csum_size);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), 0U);
	KUNIT_EXPECT_EQ(test, ext4_rec_len_from_disk(de->rec_len, TEST_BLK_SIZE),
			12);

	/* Deleting a pointer not in the buffer returns -ENOENT */
	err = ext4_generic_delete_entry(dir,
			(struct ext4_dir_entry_2 *)(bh->b_data + 500),
			bh, bh->b_data, TEST_BLK_SIZE, csum_size);
	KUNIT_EXPECT_EQ(test, err, -ENOENT);

	ext4_fname_free_filename(&fname1);
	ext4_fname_free_filename(&fname2);
}

/*
 * Test 6: Directory emptiness validation (ext4_empty_dir) across fresh,
 * non-empty, deleted, corrupt, and multi-block directories.
 */
static void test_namei_empty_dir(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry parent_dentry, file_dentry;
	struct buffer_head *bh, *bh1;
	struct ext4_dir_entry_2 *de = NULL;
	ext4_lblk_t lblk;

	init_mock_dentry(&parent_dentry, NULL, "dir", dir, &ctx->sb);
	init_mock_dentry(&file_dentry, &parent_dentry, "item", file1, &ctx->sb);

	/* Freshly initialized directory is empty */
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	KUNIT_EXPECT_TRUE(test, ext4_empty_dir(dir));

	/* Truncated i_size < 24 bytes is rejected as non-empty (corrupt) */
	dir->i_size = 12;
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));
	dir->i_size = TEST_BLK_SIZE;

	/* Add an entry -> not empty */
	KUNIT_ASSERT_EQ(test, ext4_add_entry_test(NULL, &file_dentry, file1), 0);
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));

	/* Delete the entry -> empty again */
	bh = ext4_find_entry_test(dir, &file_dentry.d_name, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_ASSERT_EQ(test, ext4_delete_entry_test(NULL, dir, de, bh), 0);
	brelse(bh);
	KUNIT_EXPECT_TRUE(test, ext4_empty_dir(dir));

	/* Multi-block directory with a hole at block 1 and an empty block 2 */
	dir->i_size = 2 * TEST_BLK_SIZE; /* block 1 is unmapped (hole) */
	bh1 = ext4_append_test(NULL, dir, &lblk); /* appends block 2 */
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh1));
	KUNIT_EXPECT_EQ(test, lblk, 2U);
	de = (struct ext4_dir_entry_2 *)bh1->b_data;
	de->inode = 0;
	de->rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE, TEST_BLK_SIZE);
	KUNIT_EXPECT_TRUE(test, ext4_empty_dir(dir));

	/* Active entry in block 2 makes directory non-empty */
	de->inode = cpu_to_le32(TEST_FILE1_INO);
	de->name_len = 1;
	de->name[0] = 'x';
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));
	de->inode = 0;
	brelse(bh1);

	/* Corrupt '..' name in block 0 -> returns false */
	bh = map_and_get_dir_bh(ctx, dir, 0);
	de = ext4_next_entry((struct ext4_dir_entry_2 *)bh->b_data,
			     TEST_BLK_SIZE);
	de->name[1] = 'x';
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));
	de->name[1] = '.';

	/* Corrupt '.' inode number in block 0 -> returns false */
	de = (struct ext4_dir_entry_2 *)bh->b_data;
	de->inode = cpu_to_le32(999);
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));
}

/*
 * Test 7: HTree leaf splitting helpers: dx_make_map_test, dx_sort_map_test
 * (exercising both combsort and bubble sort phases), dx_move_dirents_test,
 * dx_pack_dirents_test, and dx_insert_block_test.
 */
static void test_namei_dx_map_pack_and_move(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct buffer_head *bh, *bh2;
	struct dx_hash_info hinfo = {};
	struct dx_map_entry *map;
	struct ext4_dir_entry_2 *de, *last_de;
	struct dx_frame frame;
	struct dx_entry entries[8];
	int i, count;

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	bh = map_and_get_dir_bh(ctx, dir, 0);
	bh2 = map_and_get_dir_bh(ctx, dir, 1);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	KUNIT_ASSERT_NOT_NULL(test, bh2);

	/* Insert 16 entries into block 0 so count >= 18 (triggers combsort) */
	for (i = 0; i < 16; i++) {
		char name[16];
		struct qstr q;
		struct ext4_filename fname;

		snprintf(name, sizeof(name), "entry_%02d", i);
		q.name = (const unsigned char *)name;
		q.len = strlen(name);
		KUNIT_ASSERT_EQ(test,
			ext4_fname_setup_filename(dir, &q, 0, &fname), 0);
		KUNIT_ASSERT_EQ(test,
			ext4_find_dest_de(dir, bh, bh->b_data, TEST_BLK_SIZE,
					  &fname, &de), 0);
		ext4_insert_dentry(dir, file1, de, TEST_BLK_SIZE, &fname);
		ext4_fname_free_filename(&fname);
	}

	hinfo.hash_version = DX_HASH_HALF_MD4 + ctx->sbi.s_hash_unsigned;
	hinfo.seed = ctx->sbi.s_hash_seed;

	map = (struct dx_map_entry *)(bh2->b_data + TEST_BLK_SIZE);
	count = dx_make_map_test(dir, bh, &hinfo, map);
	/* 2 ('.' and '..') + 16 = 18 entries */
	KUNIT_ASSERT_EQ(test, count, 18);

	map -= count;
	dx_sort_map_test(map, count);
	for (i = 1; i < count; i++)
		KUNIT_EXPECT_LE(test, map[i - 1].hash, map[i].hash);

	/* Move the second half (9 entries) to bh2 and pack the first half */
	last_de = dx_move_dirents_test(dir, bh->b_data, bh2->b_data,
				       map + 9, 9, TEST_BLK_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, last_de);
	last_de->rec_len = ext4_rec_len_to_disk(
		bh2->b_data + TEST_BLK_SIZE - (char *)last_de, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test,
		ext4_check_all_de(dir, bh2, bh2->b_data, TEST_BLK_SIZE), 0);

	last_de = dx_pack_dirents_test(dir, bh->b_data, TEST_BLK_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, last_de);
	last_de->rec_len = ext4_rec_len_to_disk(
		bh->b_data + TEST_BLK_SIZE - (char *)last_de, TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test,
		ext4_check_all_de(dir, bh, bh->b_data, TEST_BLK_SIZE), 0);

	/* Test dx_insert_block_test shifting entries in a dx_frame */
	memset(entries, 0, sizeof(entries));
	dx_set_limit(entries, 8);
	dx_set_count(entries, 2);
	dx_set_block(entries + 0, 1);
	dx_set_hash(entries + 1, 0x80000000U);
	dx_set_block(entries + 1, 2);

	frame.entries = entries;
	frame.at = entries + 0;
	dx_insert_block_test(&frame, 0x40000000U, 3);
	KUNIT_EXPECT_EQ(test, dx_get_count(entries), 3U);
	KUNIT_EXPECT_EQ(test, dx_get_hash(entries + 1), 0x40000000U);
	KUNIT_EXPECT_EQ(test, dx_get_block(entries + 1), 3U);
	KUNIT_EXPECT_EQ(test, dx_get_hash(entries + 2), 0x80000000U);
	KUNIT_EXPECT_EQ(test, dx_get_block(entries + 2), 2U);
}

/*
 * Test 8: Parameterized tests for ext4_check_dx_root_test, dx_probe_test, and
 * ext4_htree_next_block_test across valid 1-level / 2-level trees and 10
 * on-disk corruption cases.
 */
enum dx_probe_case_id {
	DXP_VALID_1LEVEL,
	DXP_VALID_2LEVEL_AND_NEXT,
	DXP_DOT_BAD_NAMELEN,
	DXP_DOT_BAD_NAME,
	DXP_DOT_BAD_RECLEN,
	DXP_DOTDOT_BAD_NAMELEN,
	DXP_DOTDOT_BAD_NAME,
	DXP_BAD_HASH_VERSION,
	DXP_SIPHASH_WITHOUT_IN_DIRENT,
	DXP_UNUSED_FLAGS_SET,
	DXP_INDIRECT_LEVEL_TOO_DEEP,
	DXP_BAD_ROOT_LIMIT,
	DXP_ZERO_COUNT,
	DXP_TREE_CYCLE,
};

struct dx_probe_param {
	const char *desc;
	enum dx_probe_case_id id;
	bool check_dx_root_only;
	bool expect_ok;
};

static const struct dx_probe_param dx_probe_params[] = {
	{
		.desc = "valid 1-level dx_root binary search probe",
		.id = DXP_VALID_1LEVEL,
		.expect_ok = true,
	},
	{
		.desc = "valid 2-level htree probe and ext4_htree_next_block",
		.id = DXP_VALID_2LEVEL_AND_NEXT,
		.expect_ok = true,
	},
	{
		.desc = "ext4_check_dx_root rejects dot name_len != 1",
		.id = DXP_DOT_BAD_NAMELEN,
		.check_dx_root_only = true,
		.expect_ok = false,
	},
	{
		.desc = "ext4_check_dx_root rejects dot name != '.'",
		.id = DXP_DOT_BAD_NAME,
		.check_dx_root_only = true,
		.expect_ok = false,
	},
	{
		.desc = "ext4_check_dx_root rejects dot rec_len >= blocksize",
		.id = DXP_DOT_BAD_RECLEN,
		.check_dx_root_only = true,
		.expect_ok = false,
	},
	{
		.desc = "ext4_check_dx_root rejects dotdot name_len != 2",
		.id = DXP_DOTDOT_BAD_NAMELEN,
		.check_dx_root_only = true,
		.expect_ok = false,
	},
	{
		.desc = "ext4_check_dx_root rejects dotdot name != '..'",
		.id = DXP_DOTDOT_BAD_NAME,
		.check_dx_root_only = true,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects unrecognized hash_version",
		.id = DXP_BAD_HASH_VERSION,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects DX_HASH_SIPHASH when not hash_in_dirent",
		.id = DXP_SIPHASH_WITHOUT_IN_DIRENT,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects non-zero unused_flags & 1",
		.id = DXP_UNUSED_FLAGS_SET,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects indirect_levels >= ext4_dir_htree_level",
		.id = DXP_INDIRECT_LEVEL_TOO_DEEP,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects root limit mismatch",
		.id = DXP_BAD_ROOT_LIMIT,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe rejects count == 0",
		.id = DXP_ZERO_COUNT,
		.expect_ok = false,
	},
	{
		.desc = "dx_probe detects tree cycle pointing back to block 0",
		.id = DXP_TREE_CYCLE,
		.expect_ok = false,
	},
};

static void dx_probe_get_desc(const struct dx_probe_param *p, char *desc)
{
	strscpy(desc, p->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(dx_probe, dx_probe_params, dx_probe_get_desc);

static void setup_valid_dx_root(struct inode *dir, struct buffer_head *bh)
{
	struct dx_root *root = (struct dx_root *)bh->b_data;

	memset(root, 0, TEST_BLK_SIZE);
	root->dot.inode = cpu_to_le32(dir->i_ino);
	root->dot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
	root->dot.name_len = 1;
	root->dot.file_type = EXT4_FT_DIR;
	strscpy(root->dot_name, ".", sizeof(root->dot_name));

	root->dotdot.inode = cpu_to_le32(TEST_ROOT_INO);
	root->dotdot.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE - 12,
						    TEST_BLK_SIZE);
	root->dotdot.name_len = 2;
	root->dotdot.file_type = EXT4_FT_DIR;
	strscpy(root->dotdot_name, "..", sizeof(root->dotdot_name));

	root->info.info_length = sizeof(struct dx_root_info);
	root->info.hash_version = DX_HASH_HALF_MD4;
	dx_set_limit(root->entries,
		     dx_root_limit(dir, sizeof(struct dx_root_info)));
	dx_set_count(root->entries, 2);
	dx_set_block(root->entries + 0, 1);
	dx_set_hash(root->entries + 1, 0x80000000U);
	dx_set_block(root->entries + 1, 2);
}

static void test_namei_check_dx_root_and_probe(struct kunit *test)
{
	const struct dx_probe_param *p = test->param_value;
	struct namei_test_ctx *ctx = test->priv;
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct buffer_head *bh0, *bh1;
	struct dx_root *root;
	struct dx_node *node;
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct dx_hash_info hinfo = {};
	__u32 start_hash = 0;

	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	dir->i_size = 4 * TEST_BLK_SIZE;
	bh0 = map_and_get_dir_bh(ctx, dir, 0);
	KUNIT_ASSERT_NOT_NULL(test, bh0);
	setup_valid_dx_root(dir, bh0);
	root = (struct dx_root *)bh0->b_data;

	switch (p->id) {
	case DXP_VALID_1LEVEL:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		KUNIT_EXPECT_TRUE(test, ext4_check_dx_root_test(dir, root));
		root->dotdot.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE - 12,
							    TEST_BLK_SIZE);
		break;
	case DXP_VALID_2LEVEL_AND_NEXT:
		root->info.indirect_levels = 1;
		bh1 = map_and_get_dir_bh(ctx, dir, 1);
		KUNIT_ASSERT_NOT_NULL(test, bh1);
		node = (struct dx_node *)bh1->b_data;
		node->fake.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE,
							  TEST_BLK_SIZE);
		dx_set_limit(node->entries, dx_node_limit(dir));
		dx_set_count(node->entries, 2);
		dx_set_block(node->entries + 0, 2);
		/* Continuation hash (low bit 1) matching 0x40000000 */
		dx_set_hash(node->entries + 1, 0x40000001U);
		dx_set_block(node->entries + 1, 3);
		break;
	case DXP_DOT_BAD_NAMELEN:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dot.name_len = 2;
		break;
	case DXP_DOT_BAD_NAME:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dot_name[0] = 'x';
		break;
	case DXP_DOT_BAD_RECLEN:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dot.rec_len = ext4_rec_len_to_disk(TEST_BLK_SIZE,
							 TEST_BLK_SIZE);
		break;
	case DXP_DOTDOT_BAD_NAMELEN:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dotdot.name_len = 1;
		break;
	case DXP_DOTDOT_BAD_NAME:
		root->dotdot.rec_len = ext4_rec_len_to_disk(12, TEST_BLK_SIZE);
		root->dotdot_name[1] = 'x';
		break;
	case DXP_BAD_HASH_VERSION:
		root->info.hash_version = 99;
		break;
	case DXP_SIPHASH_WITHOUT_IN_DIRENT:
		root->info.hash_version = DX_HASH_SIPHASH;
		break;
	case DXP_UNUSED_FLAGS_SET:
		root->info.unused_flags = 1;
		break;
	case DXP_INDIRECT_LEVEL_TOO_DEEP:
		root->info.indirect_levels = EXT4_HTREE_LEVEL_COMPAT;
		break;
	case DXP_BAD_ROOT_LIMIT:
		dx_set_limit(root->entries, 50);
		break;
	case DXP_ZERO_COUNT:
		dx_set_count(root->entries, 0);
		break;
	case DXP_TREE_CYCLE:
		dx_set_block(root->entries + 0, 0);
		break;
	}

	if (p->check_dx_root_only) {
		KUNIT_EXPECT_EQ(test, ext4_check_dx_root_test(dir, root),
				p->expect_ok);
		return;
	}

	hinfo.hash = 0x40000000U;
	frame = dx_probe_test(NULL, dir, &hinfo, frames);
	if (!p->expect_ok) {
		KUNIT_EXPECT_TRUE(test, IS_ERR(frame));
		KUNIT_EXPECT_EQ(test, PTR_ERR(frame), ERR_BAD_DX_DIR);
		return;
	}

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(frame));
	if (p->id == DXP_VALID_1LEVEL) {
		KUNIT_EXPECT_PTR_EQ(test, frame, &frames[0]);
		KUNIT_EXPECT_EQ(test, dx_get_block(frame->at), 1U);
	} else if (p->id == DXP_VALID_2LEVEL_AND_NEXT) {
		KUNIT_EXPECT_PTR_EQ(test, frame, &frames[1]);
		KUNIT_EXPECT_EQ(test, dx_get_block(frame->at), 2U);

		/* Advance to next leaf block matching continuation hash */
		KUNIT_EXPECT_EQ(test,
			ext4_htree_next_block_test(dir, 0x40000000U, frame,
						   frames, &start_hash), 1);
		KUNIT_EXPECT_EQ(test, start_hash, 0x40000001U);
		KUNIT_EXPECT_EQ(test, dx_get_block(frame->at), 3U);

		/* Non-matching even hash stops traversal */
		KUNIT_EXPECT_EQ(test,
			ext4_htree_next_block_test(dir, 0x40000000U, frame,
						   frames, NULL), 0);
	}
	dx_release_test(frames);
}

/*
 * Test 9: End-to-end HTree directory conversion (make_indexed_dir), multiple
 * leaf splits (ext4_dx_add_entry / do_split), indexed lookup
 * (ext4_dx_find_entry), and rb-tree population (ext4_htree_fill_tree).
 */
static void test_namei_add_find_and_htree_split(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry *dentries;
	struct dentry *parent_dentry, *child_dentry;
	struct dir_private_info *dir_info;
	struct file *dir_file;
	__u32 curr_hash = 0, next_hash = 0;
	int i, err, total_loaded = 0;

	dentries = kunit_kcalloc(test, 2, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	parent_dentry = &dentries[0];
	child_dentry = &dentries[1];
	dir_file = kunit_kzalloc(test, sizeof(*dir_file), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dir_file);

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	init_mock_dentry(parent_dentry, NULL, "dir", dir, &ctx->sb);

	/*
	 * Insert 60 entries into a 1024-byte block directory.
	 * Each entry takes 20 bytes (name_len = 10 -> rec_len = 20), so ~48
	 * entries fit in block 0 before make_indexed_dir converts it into an
	 * HTree and subsequent insertions split leaves via ext4_dx_add_entry.
	 */
	for (i = 0; i < 60; i++) {
		char name[16];

		snprintf(name, sizeof(name), "file_%05d", i);
		init_mock_dentry(child_dentry, parent_dentry, name, file1,
				 &ctx->sb);
		err = ext4_add_entry_test(NULL, child_dentry, file1);
		KUNIT_ASSERT_EQ_MSG(test, err, 0, "failed at entry %d", i);
	}

	/* Verify the directory was converted to an HTree index */
	KUNIT_EXPECT_TRUE(test, ext4_test_inode_flag(dir, EXT4_INODE_INDEX));
	KUNIT_EXPECT_GE(test, dir->i_size, (loff_t)(3 * TEST_BLK_SIZE));

	/* Adding a duplicate entry must return -EEXIST */
	init_mock_dentry(child_dentry, parent_dentry, "file_00025", file1,
			 &ctx->sb);
	KUNIT_EXPECT_EQ(test,
			ext4_add_entry_test(NULL, child_dentry, file1),
			-EEXIST);

	/* Look up all 60 entries via ext4_find_entry_test (ext4_dx_find_entry) */
	for (i = 0; i < 60; i++) {
		char name[16];
		struct qstr q;
		struct ext4_dir_entry_2 *de = NULL;
		struct buffer_head *bh;

		snprintf(name, sizeof(name), "file_%05d", i);
		q.name = (const unsigned char *)name;
		q.len = strlen(name);
		bh = ext4_find_entry_test(dir, &q, &de, NULL);
		KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(bh),
				       "missing entry %s", name);
		KUNIT_ASSERT_NOT_NULL(test, de);
		KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
		brelse(bh);
	}

	/* Populate readdir rb-tree via ext4_htree_fill_tree */
	dir_file->f_inode = dir;
	while (1) {
		int cnt;

		dir_info = kzalloc_obj(*dir_info);
		KUNIT_ASSERT_NOT_NULL(test, dir_info);
		dir_file->private_data = dir_info;
		cnt = ext4_htree_fill_tree(dir_file, curr_hash, 0, &next_hash);
		KUNIT_ASSERT_GE(test, cnt, 0);
		total_loaded += cnt;
		ext4_htree_free_dir_info(dir_info);
		if (next_hash == ~0U)
			break;
		curr_hash = next_hash;
	}
	/* 60 files + '.' + '..' = 62 total entries */
	KUNIT_EXPECT_EQ(test, total_loaded, 62);
}

/*
 * Test 10: HTree index level growth (splitting full dx_root into a 2-level
 * tree with indirect_levels == 1), max htree level -ENOSPC enforcement, and
 * corrupted dx_root fallback in __ext4_find_entry and __ext4_add_entry.
 */
static void test_namei_dx_index_level_growth_and_fallback(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry parent_dentry, child_dentry;
	struct buffer_head *bh0, *bh1, *found_bh;
	struct dx_root *root;
	struct ext4_dir_entry_2 *de = NULL;
	unsigned int root_limit;
	int i, err;

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	init_mock_dentry(&parent_dentry, NULL, "dir", dir, &ctx->sb);

	/* Set up a 1-level HTree where root->entries is completely full */
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	dir->i_size = 2 * TEST_BLK_SIZE;
	bh0 = map_and_get_dir_bh(ctx, dir, 0);
	bh1 = map_and_get_dir_bh(ctx, dir, 1);
	KUNIT_ASSERT_NOT_NULL(test, bh0);
	KUNIT_ASSERT_NOT_NULL(test, bh1);

	setup_valid_dx_root(dir, bh0);
	root = (struct dx_root *)bh0->b_data;
	root_limit = dx_root_limit(dir, sizeof(struct dx_root_info));
	dx_set_limit(root->entries, root_limit);
	dx_set_count(root->entries, root_limit);
	/* Point every index entry at leaf block 1 in ascending hash order */
	for (i = 0; i < root_limit; i++) {
		if (i > 0)
			dx_set_hash(root->entries + i, (u32)i * 0x02000000U);
		dx_set_block(root->entries + i, 1);
	}
	ext4_dx_csum_set_test(dir, (struct ext4_dir_entry *)bh0->b_data);

	/* Fill leaf block 1 completely so adding one more entry splits it */
	KUNIT_ASSERT_EQ(test,
		ext4_init_dirblock(NULL, dir, bh1, TEST_ROOT_INO, NULL, 0), 0);
	for (i = 0; i < 60; i++) {
		char name[16];
		struct qstr q;
		struct ext4_filename fname;

		snprintf(name, sizeof(name), "leaf_%05d", i);
		q.name = (const unsigned char *)name;
		q.len = strlen(name);
		KUNIT_ASSERT_EQ(test,
			ext4_fname_setup_filename(dir, &q, 0, &fname), 0);
		err = ext4_find_dest_de(dir, bh1, bh1->b_data,
					TEST_BLK_SIZE -
					sizeof(struct ext4_dir_entry_tail),
					&fname, &de);
		if (err == 0)
			ext4_insert_dentry(dir, file1, de, TEST_BLK_SIZE,
					   &fname);
		ext4_fname_free_filename(&fname);
		if (err == -ENOSPC)
			break;
	}
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh1), 0);

	/*
	 * Adding "trigger_split_entry" finds leaf block 1 full and root index
	 * full (count == limit), triggering the add_level branch in
	 * ext4_dx_add_entry (indirect_levels: 0 -> 1).
	 */
	init_mock_dentry(&child_dentry, &parent_dentry, "trigger_split_entry",
			 file1, &ctx->sb);
	err = ext4_add_entry_test(NULL, &child_dentry, file1);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, root->info.indirect_levels, 1);
	KUNIT_EXPECT_EQ(test, dx_get_count(root->entries), 1U);

	/* Verify the newly inserted entry can be found in the 2-level tree */
	found_bh = ext4_find_entry_test(dir, &child_dentry.d_name, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(found_bh));
	brelse(found_bh);

	/*
	 * Test interior dx_node split (!add_level in ext4_dx_add_entry) and
	 * max htree level (-ENOSPC when levels == ext4_dir_htree_level(sb)):
	 * Block 2 is the new interior dx_node created by the root split above.
	 * Point all node_limit entries in block 2 to leaf block 1 and refill
	 * leaf block 1 so the next insertion splits the interior dx_node.
	 */
	{
		struct buffer_head *bh2 = map_and_get_dir_bh(ctx, dir, 2);
		struct dx_node *node;
		unsigned int node_limit = dx_node_limit(dir);

		KUNIT_ASSERT_NOT_NULL(test, bh2);
		node = (struct dx_node *)bh2->b_data;
		dx_set_limit(node->entries, node_limit);
		dx_set_count(node->entries, node_limit);
		for (i = 0; i < node_limit; i++) {
			if (i > 0)
				dx_set_hash(node->entries + i,
					    (u32)i * 0x01000000U);
			dx_set_block(node->entries + i, 1);
		}
		ext4_dx_csum_set_test(dir,
				      (struct ext4_dir_entry *)bh2->b_data);

		/* Refill leaf block 1 to capacity */
		for (i = 60; i < 120; i++) {
			char name[16];
			struct qstr q;
			struct ext4_filename fname;

			snprintf(name, sizeof(name), "leaf_%05d", i);
			q.name = (const unsigned char *)name;
			q.len = strlen(name);
			KUNIT_ASSERT_EQ(test,
				ext4_fname_setup_filename(dir, &q, 0, &fname),
				0);
			err = ext4_find_dest_de(dir, bh1, bh1->b_data,
						TEST_BLK_SIZE -
						sizeof(struct ext4_dir_entry_tail),
						&fname, &de);
			if (err == 0)
				ext4_insert_dentry(dir, file1, de,
						   TEST_BLK_SIZE, &fname);
			ext4_fname_free_filename(&fname);
			if (err == -ENOSPC)
				break;
		}
		KUNIT_ASSERT_EQ(test,
				ext4_handle_dirty_dirblock(NULL, dir, bh1), 0);

		/*
		 * First test max htree level check: if both root (level 0) and
		 * interior node (level 1) are full when LARGEDIR is disabled
		 * (max level 2), ext4_dx_add_entry returns -ENOSPC.
		 */
		dx_set_count(root->entries, root_limit);
		for (i = 0; i < root_limit; i++) {
			if (i > 0)
				dx_set_hash(root->entries + i,
					    (u32)i * 0x02000000U);
			dx_set_block(root->entries + i, 2);
		}
		ext4_dx_csum_set_test(dir,
				      (struct ext4_dir_entry *)bh0->b_data);
		init_mock_dentry(&child_dentry, &parent_dentry,
				 "max_level_enospc", file1, &ctx->sb);
		KUNIT_EXPECT_EQ(test,
				ext4_add_entry_test(NULL, &child_dentry, file1),
				-ENOSPC);

		/*
		 * Restore root count to 1 and insert again: now root has room
		 * while interior node at block 2 is full, exercising the
		 * !add_level interior node split in ext4_dx_add_entry.
		 */
		dx_set_count(root->entries, 1);
		dx_set_block(root->entries + 0, 2);
		ext4_dx_csum_set_test(dir,
				      (struct ext4_dir_entry *)bh0->b_data);
		init_mock_dentry(&child_dentry, &parent_dentry,
				 "interior_node_split", file1, &ctx->sb);
		KUNIT_ASSERT_EQ(test,
				ext4_add_entry_test(NULL, &child_dentry, file1),
				0);
		KUNIT_EXPECT_EQ(test, root->info.indirect_levels, 1);
		KUNIT_EXPECT_EQ(test, dx_get_count(root->entries), 2U);
	}

	/*
	 * Now test corrupted dx_root handling:
	 * 1) With METADATA_CSUM enabled, __ext4_add_entry refuses to clear
	 *    EXT4_INODE_INDEX and returns -EFSCORRUPTED.
	 */
	root->info.hash_version = 99;
	ext4_dx_csum_set_test(dir, (struct ext4_dir_entry *)bh0->b_data);
	init_mock_dentry(&child_dentry, &parent_dentry, "after_corrupt",
			 file1, &ctx->sb);
	KUNIT_EXPECT_EQ(test,
			ext4_add_entry_test(NULL, &child_dentry, file1),
			-EFSCORRUPTED);

	/*
	 * 2) __ext4_find_entry falls back to linear block scan on
	 *    ERR_BAD_DX_DIR and still finds "leaf_00000" in block 1!
	 */
	init_mock_dentry(&child_dentry, &parent_dentry, "leaf_00000",
			 file1, &ctx->sb);
	found_bh = ext4_find_entry_test(dir, &child_dentry.d_name, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(found_bh));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
	brelse(found_bh);

	/*
	 * 3) Without METADATA_CSUM, __ext4_add_entry clears EXT4_INODE_INDEX
	 *    on ERR_BAD_DX_DIR and falls back to linear append.
	 */
	ctx->es.s_feature_ro_compat &=
		~cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
	init_mock_dentry(&child_dentry, &parent_dentry, "fallback_new_entry",
			 file1, &ctx->sb);
	KUNIT_EXPECT_EQ(test,
			ext4_add_entry_test(NULL, &child_dentry, file1), 0);
	KUNIT_EXPECT_FALSE(test, ext4_test_inode_flag(dir, EXT4_INODE_INDEX));
}

/*
 * Test 11: Inline directory lifecycle through namei.c entry points
 * (ext4_init_new_dir, ext4_empty_dir, ext4_add_entry_test,
 * ext4_find_entry_test, ext4_delete_entry_test, ext4_htree_fill_tree).
 */
static void test_namei_inline_dir_lifecycle(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry parent_dentry, child_dentry;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de = NULL;
	struct dir_private_info *dir_info;
	struct file dir_file = {};
	__u32 next_hash = 0;
	int inlined = 0;

	ctx->es.s_feature_incompat |=
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_INLINE_DATA);
	ext4_set_inode_state(dir, EXT4_STATE_MAY_INLINE_DATA);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(dir));
	KUNIT_EXPECT_TRUE(test, ext4_empty_dir(dir));

	init_mock_dentry(&parent_dentry, NULL, "idir", dir, &ctx->sb);
	init_mock_dentry(&child_dentry, &parent_dentry, "tiny", file1,
			 &ctx->sb);

	/* Add an entry into the inline directory */
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, &child_dentry, file1), 0);
	KUNIT_EXPECT_TRUE(test, ext4_has_inline_data(dir));
	KUNIT_EXPECT_FALSE(test, ext4_empty_dir(dir));

	/* Find the inline entry */
	bh = ext4_find_entry_test(dir, &child_dentry.d_name, &de, &inlined);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_EXPECT_EQ(test, inlined, 1);
	KUNIT_ASSERT_NOT_NULL(test, de);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
	brelse(bh);

	/* Load inline directory into rb-tree via ext4_htree_fill_tree */
	dir_info = kzalloc_obj(*dir_info);
	KUNIT_ASSERT_NOT_NULL(test, dir_info);
	dir_file.f_inode = dir;
	dir_file.private_data = dir_info;
	KUNIT_EXPECT_EQ(test,
			ext4_htree_fill_tree(&dir_file, 0, 0, &next_hash), 3);
	KUNIT_EXPECT_EQ(test, next_hash, ~0U);
	ext4_htree_free_dir_info(dir_info);

	/* Delete the inline entry -> directory becomes empty again */
	bh = ext4_find_entry_test(dir, &child_dentry.d_name, &de, &inlined);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_ASSERT_EQ(test, ext4_delete_entry_test(NULL, dir, de, bh), 0);
	brelse(bh);
	KUNIT_EXPECT_TRUE(test, ext4_empty_dir(dir));
}

/*
 * Test 12: Directory link count helpers (ext4_inc_count_test,
 * ext4_dec_count_test) and VFS lookup / parent resolution (ext4_lookup,
 * ext4_get_parent).
 */
static void test_namei_nlink_and_lookup_parent(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry *dentries;
	struct dentry *parent_dentry, *lookup_dentry, *res_dentry;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de = NULL;
	char *long_name;

	dentries = kunit_kcalloc(test, 2, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	parent_dentry = &dentries[0];
	lookup_dentry = &dentries[1];
	long_name = kunit_kzalloc(test, EXT4_NAME_LEN + 2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, long_name);

	/* Test ext4_inc_count_test and ext4_dec_count_test on indexed dir */
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	set_nlink(dir, 2);
	ext4_inc_count_test(dir);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 3U);
	ext4_dec_count_test(dir);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 2U);

	/* Overflow past EXT4_LINK_MAX wraps nlink to 1 and keeps it at 1 */
	set_nlink(dir, EXT4_LINK_MAX);
	ext4_inc_count_test(dir);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 1U);
	ext4_inc_count_test(dir);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 1U);
	ext4_dec_count_test(dir);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 1U);
	ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);

	/* Initialize directory for lookup and get_parent tests */
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	init_mock_dentry(parent_dentry, NULL, "dir", dir, &ctx->sb);

	/* 1) Name longer than EXT4_NAME_LEN -> -ENAMETOOLONG */
	memset(long_name, 'a', EXT4_NAME_LEN + 1);
	long_name[EXT4_NAME_LEN + 1] = '\0';
	init_mock_dentry(lookup_dentry, parent_dentry, long_name, NULL,
			 &ctx->sb);
	res_dentry = ext4_dir_inode_operations.lookup(dir, lookup_dentry, 0);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-ENAMETOOLONG));

	/* 2) Negative lookup returns NULL without calling ext4_iget */
	ctx->last_iget_ino = 0;
	init_mock_dentry(lookup_dentry, parent_dentry, "nonexistent", NULL,
			 &ctx->sb);
	res_dentry = ext4_dir_inode_operations.lookup(dir, lookup_dentry, 0);
	/*
	 * ext4_lookup() hashes negative dentries via d_splice_alias(); unhash
	 * it and wait for RCU readers before reusing or freeing memory.
	 */
	d_drop(lookup_dentry);
	synchronize_rcu();
	KUNIT_EXPECT_NULL(test, res_dentry);
	KUNIT_EXPECT_EQ(test, ctx->last_iget_ino, 0UL);

	/* 3) Positive lookup invokes ext4_iget with target inode number */
	init_mock_dentry(lookup_dentry, parent_dentry, "target", file1,
			 &ctx->sb);
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, lookup_dentry, file1), 0);
	ctx->fail_iget = -ESTALE;
	/* -ESTALE from ext4_iget is translated to -EFSCORRUPTED */
	res_dentry = ext4_dir_inode_operations.lookup(dir, lookup_dentry, 0);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-EFSCORRUPTED));
	KUNIT_EXPECT_EQ(test, ctx->last_iget_ino, TEST_FILE1_INO);

	/* 4) Self-linked directory entry (de->inode == dir->i_ino) -> -EFSCORRUPTED */
	bh = ext4_find_entry_test(dir, &lookup_dentry->d_name, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	de->inode = cpu_to_le32(dir->i_ino);
	brelse(bh);
	res_dentry = ext4_dir_inode_operations.lookup(dir, lookup_dentry, 0);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-EFSCORRUPTED));

	/* 5) Invalid inode number (e.g. reserved ino 5) -> -EFSCORRUPTED */
	de->inode = cpu_to_le32(5);
	res_dentry = ext4_dir_inode_operations.lookup(dir, lookup_dentry, 0);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-EFSCORRUPTED));

	/* 6) ext4_get_parent looks up ".." and calls ext4_iget(TEST_ROOT_INO) */
	ctx->fail_iget = -EUCLEAN;
	ctx->last_iget_ino = 0;
	res_dentry = ext4_get_parent(parent_dentry);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-EUCLEAN));
	KUNIT_EXPECT_EQ(test, ctx->last_iget_ino, (unsigned long)TEST_ROOT_INO);

	/* Corrupt '..' inode to invalid ino 5 -> ext4_get_parent returns -EFSCORRUPTED */
	bh = map_and_get_dir_bh(ctx, dir, 0);
	de = ext4_next_entry((struct ext4_dir_entry_2 *)bh->b_data,
			     TEST_BLK_SIZE);
	de->inode = cpu_to_le32(5);
	res_dentry = ext4_get_parent(parent_dentry);
	KUNIT_EXPECT_PTR_EQ(test, res_dentry, ERR_PTR(-EFSCORRUPTED));
}

/*
 * Test 13: Hard link, unlink, and rmdir operations (__ext4_link,
 * ext4_dir_inode_operations.link, __ext4_unlink,
 * ext4_dir_inode_operations.unlink, ext4_dir_inode_operations.rmdir).
 */
static void test_namei_link_unlink_and_rmdir(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *subdir = get_mock_inode(ctx, TEST_SUBDIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct dentry *dentries;
	struct dentry *dir_dentry, *f1_dentry, *f1_link_dentry, *sub_dentry;
	int err;

	dentries = kunit_kcalloc(test, 4, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	dir_dentry = &dentries[0];
	f1_dentry = &dentries[1];
	f1_link_dentry = &dentries[2];
	sub_dentry = &dentries[3];

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, dir, subdir), 0);
	ext4_inc_count_test(dir);

	init_mock_dentry(dir_dentry, NULL, "dir", dir, &ctx->sb);
	init_mock_dentry(f1_dentry, dir_dentry, "orig", file1, &ctx->sb);
	init_mock_dentry(f1_link_dentry, dir_dentry, "hardlink", file1,
			 &ctx->sb);
	init_mock_dentry(sub_dentry, dir_dentry, "subdir", subdir, &ctx->sb);

	/* Link a 0-nlink tmpfile -> nlink becomes 1 and ext4_orphan_del runs */
	set_nlink(file1, 0);
	inode_state_set_raw(file1, I_DIRTY | I_LINKABLE);
	ctx->orphan_del_calls = 0;
	err = __ext4_link(dir, file1, &f1_dentry->d_name, f1_dentry);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, file1->i_nlink, 1U);
	KUNIT_EXPECT_EQ(test, ctx->orphan_del_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_orphan_del_ino, TEST_FILE1_INO);

	/* Create a second hard link via __ext4_link */
	err = __ext4_link(dir, file1, &f1_link_dentry->d_name, f1_link_dentry);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, file1->i_nlink, 2U);

	/* Cross-project link with EXT4_INODE_PROJINHERIT returns -EXDEV */
	ext4_set_inode_flag(dir, EXT4_INODE_PROJINHERIT);
	EXT4_I(file1)->i_projid = make_kprojid(&init_user_ns, 42);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.link(f1_dentry, dir,
					       f1_link_dentry), -EXDEV);
	EXT4_I(file1)->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	/* Max link count check returns -EMLINK */
	set_nlink(file1, EXT4_LINK_MAX);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.link(f1_dentry, dir,
					       f1_link_dentry), -EMLINK);
	set_nlink(file1, 2);

	/* Unlink first link -> nlink drops to 1, not orphaned yet */
	ctx->orphan_add_calls = 0;
	err = ext4_dir_inode_operations.unlink(dir, f1_dentry);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, file1->i_nlink, 1U);
	KUNIT_EXPECT_EQ(test, ctx->orphan_add_calls, 0U);

	/* Unlink last link -> nlink drops to 0 and inode is added to orphans */
	err = ext4_dir_inode_operations.unlink(dir, f1_link_dentry);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, file1->i_nlink, 0U);
	KUNIT_EXPECT_EQ(test, ctx->orphan_add_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_orphan_add_ino, TEST_FILE1_INO);

	/* Unlinking missing entry returns -ENOENT */
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.unlink(dir, f1_link_dentry),
		-ENOENT);

	/*
	 * Test __ext4_unlink when directory entry points to a different inode:
	 * normally rejected with -ENOENT, but permitted during EXT4_FC_REPLAY.
	 */
	{
		struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);

		set_nlink(file1, 1);
		set_nlink(file2, 1);
		KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, f1_dentry, file1), 0);
		KUNIT_EXPECT_EQ(test,
			__ext4_unlink(dir, &f1_dentry->d_name, file2,
				      f1_dentry), -ENOENT);
		ctx->sbi.s_mount_state |= EXT4_FC_REPLAY;
		KUNIT_EXPECT_EQ(test,
			__ext4_unlink(dir, &f1_dentry->d_name, file2,
				      f1_dentry), 0);
		ctx->sbi.s_mount_state &= ~EXT4_FC_REPLAY;
	}

	/* Add subdir to dir and test rmdir */
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, sub_dentry, subdir), 0);

	/* Non-empty subdir cannot be removed (-ENOTEMPTY) */
	init_mock_dentry(f1_dentry, sub_dentry, "inside", file1, &ctx->sb);
	set_nlink(file1, 1);
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, f1_dentry, file1), 0);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.rmdir(dir, sub_dentry), -ENOTEMPTY);

	/* Unlink file inside subdir, then rmdir subdir succeeds */
	KUNIT_ASSERT_EQ(test,
		ext4_dir_inode_operations.unlink(subdir, f1_dentry), 0);
	KUNIT_ASSERT_EQ(test,
		ext4_dir_inode_operations.rmdir(dir, sub_dentry), 0);
	KUNIT_EXPECT_EQ(test, subdir->i_nlink, 0U);
	KUNIT_EXPECT_EQ(test, subdir->i_size, 0LL);
	KUNIT_EXPECT_EQ(test, dir->i_nlink, 2U);
	KUNIT_EXPECT_EQ(test, ctx->last_orphan_add_ino, TEST_SUBDIR_INO);
}

/*
 * Test 14: Rename, overwrite rename, cross-directory subdirectory rename,
 * RENAME_EXCHANGE (ext4_cross_rename), and slow symlink block initialization
 * (ext4_init_symlink_block_test).
 */
static void test_namei_rename_and_exchange(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir1 = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *dir2 = get_mock_inode(ctx, TEST_DIR2_INO);
	struct inode *subdir = get_mock_inode(ctx, TEST_SUBDIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct inode *file2 = get_mock_inode(ctx, TEST_FILE2_INO);
	struct inode *symlink_inode = get_mock_inode(ctx, TEST_FILE3_INO);
	struct dentry *dentries;
	struct dentry *d1_dentry, *d2_dentry;
	struct dentry *old_dentry, *new_dentry, *sub_dentry;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de = NULL;
	const char *sym_target = "/very/long/symlink/target/path/for/ext4/test";
	struct fscrypt_str disk_link = {
		.name = (unsigned char *)sym_target,
		.len = strlen(sym_target) + 1,
	};
	int err;

	dentries = kunit_kcalloc(test, 5, sizeof(*dentries), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dentries);
	d1_dentry = &dentries[0];
	d2_dentry = &dentries[1];
	old_dentry = &dentries[2];
	new_dentry = &dentries[3];
	sub_dentry = &dentries[4];

	ctx->es.s_feature_ro_compat |=
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir1), 0);
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir2), 0);
	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, dir1, subdir), 0);
	set_nlink(dir1, 3); /* '.', '..', and subdir */
	set_nlink(dir2, 2);

	init_mock_dentry(d1_dentry, NULL, "dir1", dir1, &ctx->sb);
	init_mock_dentry(d2_dentry, NULL, "dir2", dir2, &ctx->sb);

	/* Invalid rename flags are rejected with -EINVAL */
	init_mock_dentry(old_dentry, d1_dentry, "f1", file1, &ctx->sb);
	init_mock_dentry(new_dentry, d1_dentry, "f1_new", NULL, &ctx->sb);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
			old_dentry, dir1, new_dentry, ~0U), -EINVAL);

	/* Add f1 and f2 in dir1, and subdir in dir1 */
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, old_dentry, file1), 0);
	init_mock_dentry(sub_dentry, d1_dentry, "sub", subdir, &ctx->sb);
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, sub_dentry, subdir), 0);

	/* 1) Same-directory rename: f1 -> f1_renamed */
	err = ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
					       old_dentry, dir1, new_dentry,
					       0);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_NULL(test,
		ext4_find_entry_test(dir1, &old_dentry->d_name, &de, NULL));
	bh = ext4_find_entry_test(dir1, &new_dentry->d_name, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
	brelse(bh);

	/* 2) Overwrite rename: f1_renamed overwrites existing f2 in dir1 */
	init_mock_dentry(old_dentry, d1_dentry, "f1_new", file1, &ctx->sb);
	init_mock_dentry(new_dentry, d1_dentry, "f2", file2, &ctx->sb);
	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, new_dentry, file2), 0);
	ctx->orphan_add_calls = 0;
	err = ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
					       old_dentry, dir1, new_dentry,
					       0);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, file2->i_nlink, 0U);
	KUNIT_EXPECT_EQ(test, ctx->orphan_add_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->last_orphan_add_ino, TEST_FILE2_INO);

	/* 3) Cross-directory subdir move: dir1/sub -> dir2/sub_moved */
	init_mock_dentry(new_dentry, d2_dentry, "sub_moved", NULL, &ctx->sb);
	err = ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
					       sub_dentry, dir2, new_dentry,
					       0);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, dir1->i_nlink, 2U);
	KUNIT_EXPECT_EQ(test, dir2->i_nlink, 3U);

	/* Verify subdir's '..' now points to dir2 */
	bh = map_and_get_dir_bh(ctx, subdir, 0);
	de = ext4_next_entry((struct ext4_dir_entry_2 *)bh->b_data,
			     TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_DIR2_INO);

	/*
	 * 4) RENAME_EXCHANGE (ext4_cross_rename): exchange regular file
	 *    dir1/f2 with directory dir2/sub_moved across parents!
	 */
	init_mock_dentry(old_dentry, d1_dentry, "f2", file1, &ctx->sb);
	init_mock_dentry(new_dentry, d2_dentry, "sub_moved", subdir,
			 &ctx->sb);

	/* Cross-project RENAME_EXCHANGE and normal rename return -EXDEV */
	ext4_set_inode_flag(dir2, EXT4_INODE_PROJINHERIT);
	EXT4_I(dir2)->i_projid = make_kprojid(&init_user_ns, 77);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
			old_dentry, dir2, new_dentry, RENAME_EXCHANGE),
		-EXDEV);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
			old_dentry, dir2, new_dentry, 0),
		-EXDEV);
	ext4_clear_inode_flag(dir2, EXT4_INODE_PROJINHERIT);
	EXT4_I(dir2)->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	err = ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir1,
					       old_dentry, dir2, new_dentry,
					       RENAME_EXCHANGE);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, dir1->i_nlink, 3U);
	KUNIT_EXPECT_EQ(test, dir2->i_nlink, 2U);
	de = ext4_next_entry((struct ext4_dir_entry_2 *)bh->b_data,
			     TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_DIR_INO);

	/*
	 * Overwriting a target inode whose i_nlink == 0 returns -EFSCORRUPTED
	 * (in dir2, "sub_moved" now points to file1 after the exchange).
	 */
	init_mock_dentry(old_dentry, d1_dentry, "f2", subdir, &ctx->sb);
	init_mock_dentry(new_dentry, d2_dentry, "sub_moved", file1, &ctx->sb);
	set_nlink(file1, 0);
	KUNIT_EXPECT_EQ(test,
		ext4_dir_inode_operations.rename(&nop_mnt_idmap, dir2,
			new_dentry, dir2, new_dentry, 0),
		-EFSCORRUPTED);
	set_nlink(file1, 1);

	/* 5) Test ext4_init_symlink_block_test */
	err = ext4_init_symlink_block_test(NULL, symlink_inode, &disk_link);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, symlink_inode->i_size,
			(loff_t)strlen(sym_target));
	bh = map_and_get_dir_bh(ctx, symlink_inode, 0);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	KUNIT_EXPECT_STREQ(test, bh->b_data, sym_target);
}

#if IS_ENABLED(CONFIG_UNICODE)
/*
 * Test 15: Case-insensitive (EXT4_INODE_CASEFOLD) filename setup, directory
 * lookup, and strict UTF-8 validation.
 */
static void test_namei_casefold_lookup_and_strict(struct kunit *test)
{
	struct namei_test_ctx *ctx = test->priv;
	struct inode *root_dir = get_mock_inode(ctx, TEST_ROOT_INO);
	struct inode *dir = get_mock_inode(ctx, TEST_DIR_INO);
	struct inode *file1 = get_mock_inode(ctx, TEST_FILE1_INO);
	struct unicode_map *um;
	struct dentry parent_dentry, add_dentry;
	struct qstr upper_q = QSTR_INIT(" readme.md ", 11);
	struct qstr bad_utf8 = QSTR_INIT("\xff\xfe", 2);
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de = NULL;

	um = utf8_load(UNICODE_AGE(12, 1, 0));
	if (IS_ERR(um))
		kunit_skip(test, "utf8data module not available");

	ctx->sb.s_encoding = um;
	ctx->es.s_feature_incompat |=
		cpu_to_le32(EXT4_FEATURE_INCOMPAT_CASEFOLD);
	ext4_set_inode_flag(dir, EXT4_INODE_CASEFOLD);
	ext4_set_inode_flags(dir, true);

	KUNIT_ASSERT_EQ(test, ext4_init_new_dir(NULL, root_dir, dir), 0);
	init_mock_dentry(&parent_dentry, NULL, "cfdir", dir, &ctx->sb);
	init_mock_dentry(&add_dentry, &parent_dentry, " ReadMe.MD ", file1,
			 &ctx->sb);

	KUNIT_ASSERT_EQ(test,
			ext4_add_entry_test(NULL, &add_dentry, file1), 0);

	/* Case-insensitive lookup with different case finds the entry */
	bh = ext4_find_entry_test(dir, &upper_q, &de, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(bh));
	KUNIT_ASSERT_NOT_NULL(test, de);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(de->inode), TEST_FILE1_INO);
	brelse(bh);

	/* Enable SB_ENC_STRICT_MODE_FL -> invalid UTF-8 sequence is rejected */
	ctx->sb.s_encoding_flags |= SB_ENC_STRICT_MODE_FL;
	add_dentry.__d_name = bad_utf8;
	KUNIT_EXPECT_EQ(test,
			ext4_add_entry_test(NULL, &add_dentry, file1),
			-EINVAL);
}
#endif /* CONFIG_UNICODE */

static struct kunit_case namei_test_cases[] = {
	KUNIT_CASE(test_namei_dirent_tail_and_csum),
	KUNIT_CASE(test_namei_dx_helpers_and_csum),
	KUNIT_CASE_PARAM(test_namei_read_dirblock, read_dirblock_gen_params),
	KUNIT_CASE(test_namei_append_and_init_dir),
	KUNIT_CASE(test_namei_search_insert_delete_dentry),
	KUNIT_CASE(test_namei_empty_dir),
	KUNIT_CASE(test_namei_dx_map_pack_and_move),
	KUNIT_CASE_PARAM(test_namei_check_dx_root_and_probe,
			 dx_probe_gen_params),
	KUNIT_CASE(test_namei_add_find_and_htree_split),
	KUNIT_CASE(test_namei_dx_index_level_growth_and_fallback),
	KUNIT_CASE(test_namei_inline_dir_lifecycle),
	KUNIT_CASE(test_namei_nlink_and_lookup_parent),
	KUNIT_CASE(test_namei_link_unlink_and_rmdir),
	KUNIT_CASE(test_namei_rename_and_exchange),
#if IS_ENABLED(CONFIG_UNICODE)
	KUNIT_CASE(test_namei_casefold_lookup_and_strict),
#endif
	{}
};

static struct kunit_suite namei_test_suite = {
	.name = "ext4_namei_test",
	.init = namei_test_init,
	.exit = namei_test_exit,
	.test_cases = namei_test_cases,
};

kunit_test_suites(&namei_test_suite);

MODULE_LICENSE("GPL");
