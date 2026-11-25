// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 directory entry validation, linear/HTree readdir,
 * hash-cookie conversions, and directory llseek (fs/ext4/dir.c).
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/iversion.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/user_namespace.h>

#include "ext4.h"
#include "ext4_jbd2.h"

#define DIR_TEST_BLK_SIZE	1024
#define DIR_TEST_BLK_BITS	10
#define DIR_TEST_INODE_SIZE	256
#define DIR_TEST_EXTRA_ISIZE	(sizeof(struct ext4_inode) - \
				 EXT4_GOOD_OLD_INODE_SIZE)
#define DIR_TEST_FIRST_PBLK	200
#define DIR_TEST_MAX_BLOCKS	16
#define DIR_TEST_MAX_LBLOCKS	8
#define DIR_TEST_CSUM_SEED	0x4d3c2b1aU

#define DIR_TEST_ROOT_INO	EXT4_ROOT_INO
#define DIR_TEST_DIR_INO	12
#define DIR_TEST_MAX_EMIT	32

struct dir_mock_block {
	bool in_use;
	struct buffer_head bh;
	char data[DIR_TEST_BLK_SIZE] __aligned(8);
};

struct dir_emit_record {
	char name[32];
	u64 ino;
	loff_t offset;
	unsigned int d_type;
};

struct dir_capture_ctx {
	struct dir_context ctx;
	int count;
	int max_emit;
	struct inode *bump_iversion_after_first;
	struct dir_emit_record recs[DIR_TEST_MAX_EMIT];
};

struct dir_test_ctx {
	struct super_block sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct block_device bdev;
	struct address_space bd_mapping;
	struct buffer_head sb_bh;

	struct ext4_inode_info dir_ei;
	ext4_fsblk_t lblk_to_pblk[DIR_TEST_MAX_LBLOCKS];
	struct dir_mock_block blocks[DIR_TEST_MAX_BLOCKS];

	struct dentry dentry;
	struct file filp;
	struct dir_capture_ctx cap;

	int fail_map_blocks;
	int fail_getblk;
	int htree_mode;
	int htree_fill_calls;
	__u32 last_fill_start_hash;
	__u32 last_fill_start_minor;
};

enum dir_htree_stub_mode {
	DIR_HTREE_STUB_NONE = 0,
	DIR_HTREE_STUB_BAD_DX,
	DIR_HTREE_STUB_TWO_BATCH,
};

static const struct super_operations dir_test_sops = {
};

static void init_mock_dir_file(struct dir_test_ctx *ctx, fmode_t extra_mode)
{
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	struct file tmp = {
		.f_path = { .dentry = &ctx->dentry },
		.f_inode = dir,
		.f_mapping = dir->i_mapping,
		.f_op = &ext4_dir_operations,
		.f_mode = FMODE_READ | FMODE_LSEEK | FMODE_PREAD | extra_mode,
	};

	memset(&ctx->dentry, 0, sizeof(ctx->dentry));
	spin_lock_init(&ctx->dentry.d_lock);
	seqcount_spinlock_init(&ctx->dentry.d_seq, &ctx->dentry.d_lock);
	ctx->dentry.d_parent = &ctx->dentry;
	ctx->dentry.d_sb = &ctx->sb;
	ctx->dentry.d_inode = dir;
	ctx->dentry.__d_name.name = (const unsigned char *)"testdir";
	ctx->dentry.__d_name.len = 7;

	memcpy(&ctx->filp, &tmp, sizeof(ctx->filp));
	spin_lock_init(&ctx->filp.f_lock);
	mutex_init(&ctx->filp.f_pos_lock);
	/*
	 * Pre-populate the readahead window across our mock block range so
	 * ext4_readdir() does not issue block-device pagecache readahead I/O.
	 */
	ctx->filp.f_ra.start = 0;
	ctx->filp.f_ra.size = 4096;
}

static bool dir_capture_actor(struct dir_context *dctx, const char *name,
			      int namelen, loff_t offset, u64 ino,
			      unsigned int d_type)
{
	struct dir_capture_ctx *cap =
		container_of(dctx, struct dir_capture_ctx, ctx);

	if (cap->max_emit > 0 && cap->count >= cap->max_emit)
		return false;

	if (cap->count < DIR_TEST_MAX_EMIT) {
		struct dir_emit_record *rec = &cap->recs[cap->count];
		int copy_len = min_t(int, namelen, sizeof(rec->name) - 1);

		memcpy(rec->name, name, copy_len);
		rec->name[copy_len] = '\0';
		rec->ino = ino;
		rec->offset = offset;
		rec->d_type = d_type;
	}
	cap->count++;

	if (cap->bump_iversion_after_first && cap->count == 1) {
		inode_inc_iversion(cap->bump_iversion_after_first);
		cap->bump_iversion_after_first = NULL;
	}

	return true;
}

static void reset_capture(struct dir_capture_ctx *cap, loff_t pos, int max_emit)
{
	memset(cap, 0, sizeof(*cap));
	cap->ctx.actor = dir_capture_actor;
	cap->ctx.pos = pos;
	cap->max_emit = max_emit;
}

static struct buffer_head *alloc_dir_lblk(struct dir_test_ctx *ctx,
					  ext4_lblk_t lblk)
{
	int i;

	if (lblk >= DIR_TEST_MAX_LBLOCKS)
		return NULL;

	if (ctx->lblk_to_pblk[lblk]) {
		unsigned long idx = ctx->lblk_to_pblk[lblk] -
				    DIR_TEST_FIRST_PBLK;

		return &ctx->blocks[idx].bh;
	}

	for (i = 0; i < DIR_TEST_MAX_BLOCKS; i++) {
		if (!ctx->blocks[i].in_use) {
			struct buffer_head *bh = &ctx->blocks[i].bh;

			ctx->blocks[i].in_use = true;
			memset(ctx->blocks[i].data, 0, DIR_TEST_BLK_SIZE);
			set_buffer_uptodate(bh);
			clear_buffer_verified(bh);
			ctx->lblk_to_pblk[lblk] = DIR_TEST_FIRST_PBLK + i;
			return bh;
		}
	}
	return NULL;
}

static struct ext4_dir_entry_2 *write_de(char *buf, unsigned int offset,
					 u32 ino, u16 rec_len, u8 file_type,
					 const char *name)
{
	struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)(buf + offset);
	size_t nlen = strlen(name);

	memset(de, 0, rec_len);
	de->inode = cpu_to_le32(ino);
	de->rec_len = ext4_rec_len_to_disk(rec_len, DIR_TEST_BLK_SIZE);
	de->name_len = (u8)nlen;
	de->file_type = file_type;
	memcpy(de->name, name, nlen);
	return de;
}

static int store_stub_dirent(struct file *dir_file, __u32 hash,
			     __u32 minor_hash, u32 ino, u8 file_type,
			     const char *name)
{
	struct ext4_dir_entry_2 de = {
		.inode = cpu_to_le32(ino),
		.file_type = file_type,
	};
	struct fscrypt_str ent_name = {
		.name = (unsigned char *)name,
		.len = strlen(name),
	};

	return ext4_htree_store_dirent(dir_file, hash, minor_hash,
				       &de, &ent_name);
}

/* --- KUnit Static Stubs --- */

static int dir_map_blocks_stub(handle_t *handle, struct inode *inode,
			       struct ext4_map_blocks *map, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct dir_test_ctx *ctx = test->priv;
	ext4_fsblk_t pblk;

	if (ctx->fail_map_blocks)
		return ctx->fail_map_blocks;
	if (map->m_lblk >= DIR_TEST_MAX_LBLOCKS)
		return 0;

	pblk = ctx->lblk_to_pblk[map->m_lblk];
	if (!pblk) {
		map->m_pblk = 0;
		map->m_len = 1;
		map->m_flags = 0;
		return 0;
	}

	map->m_pblk = pblk;
	map->m_len = 1;
	map->m_flags = EXT4_MAP_MAPPED;
	return 1;
}

static struct buffer_head *dir_getblk_stub(handle_t *handle,
					   struct inode *inode,
					   ext4_lblk_t block, int map_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct dir_test_ctx *ctx = test->priv;
	ext4_fsblk_t pblk;
	struct buffer_head *bh;

	if (ctx->fail_getblk)
		return ERR_PTR(ctx->fail_getblk);
	if (block >= DIR_TEST_MAX_LBLOCKS)
		return NULL;

	pblk = ctx->lblk_to_pblk[block];
	if (!pblk)
		return NULL;

	bh = &ctx->blocks[pblk - DIR_TEST_FIRST_PBLK].bh;
	get_bh(bh);
	return bh;
}

static int dir_dirty_metadata_stub(const char *where, unsigned int line,
				   handle_t *handle, struct inode *inode,
				   struct buffer_head *bh)
{
	return 0;
}

static int dir_htree_fill_tree_stub(struct file *dir_file, __u32 start_hash,
				    __u32 start_minor_hash, __u32 *next_hash)
{
	struct kunit *test = kunit_get_current_test();
	struct dir_test_ctx *ctx = test->priv;
	int err;

	ctx->htree_fill_calls++;
	ctx->last_fill_start_hash = start_hash;
	ctx->last_fill_start_minor = start_minor_hash;

	if (ctx->htree_mode == DIR_HTREE_STUB_BAD_DX)
		return ERR_BAD_DX_DIR;

	if (ctx->htree_mode == DIR_HTREE_STUB_TWO_BATCH) {
		int count = 0;

		if (start_hash < 0x2000) {
			if (start_hash == 0 && start_minor_hash == 0) {
				err = store_stub_dirent(dir_file, 0, 0,
							DIR_TEST_DIR_INO,
							EXT4_FT_DIR, ".");
				if (err)
					return err;
				count++;
			}
			if (start_hash < 2 ||
			    (start_hash == 2 && start_minor_hash == 0)) {
				err = store_stub_dirent(dir_file, 2, 0,
							DIR_TEST_ROOT_INO,
							EXT4_FT_DIR, "..");
				if (err)
					return err;
				count++;
			}
			if (start_hash < 0x1000 ||
			    (start_hash == 0x1000 &&
			     start_minor_hash <= 0x55)) {
				err = store_stub_dirent(dir_file, 0x1000, 0x55,
							21, EXT4_FT_REG_FILE,
							"coll_a");
				if (err)
					return err;
				err = store_stub_dirent(dir_file, 0x1000, 0x55,
							22, EXT4_FT_SYMLINK,
							"coll_b");
				if (err)
					return err;
				count += 2;
			}
			*next_hash = 0x2000;
			return count;
		}

		if (start_hash <= 0x2000) {
			err = store_stub_dirent(dir_file, 0x2000, 0x09,
						23, EXT4_FT_DIR, "leaf2_sub");
			if (err)
				return err;
			*next_hash = ~0U;
			return 1;
		}

		*next_hash = ~0U;
		return 0;
	}

	*next_hash = ~0U;
	return 0;
}

static int dir_test_init(struct kunit *test)
{
	struct dir_test_ctx *ctx;
	struct inode *dir;
	int i;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	test->priv = ctx;

	ctx->sb.s_blocksize = DIR_TEST_BLK_SIZE;
	ctx->sb.s_blocksize_bits = DIR_TEST_BLK_BITS;
	ctx->sb.s_magic = EXT4_SUPER_MAGIC;
	ctx->sb.s_maxbytes = 64ULL * DIR_TEST_BLK_SIZE;
	ctx->sb.s_op = &dir_test_sops;
	ctx->sb.s_fs_info = &ctx->sbi;
	ctx->sb.s_user_ns = &init_user_ns;
	ctx->sb.s_bdev = &ctx->bdev;
	ctx->bdev.bd_mapping = &ctx->bd_mapping;
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	init_rwsem(&ctx->sb.s_umount);
	strscpy(ctx->sb.s_id, "ext4_dir_kunit", sizeof(ctx->sb.s_id));

	ctx->sbi.s_sb = &ctx->sb;
	ctx->sbi.s_es = &ctx->es;
	ctx->sbi.s_sbh = &ctx->sb_bh;
	ctx->sbi.s_inode_size = DIR_TEST_INODE_SIZE;
	ctx->sbi.s_first_ino = EXT4_GOOD_OLD_FIRST_INO;
	ctx->sbi.s_bitmap_maxbytes = 16ULL * DIR_TEST_BLK_SIZE;
	ctx->sbi.s_csum_seed = DIR_TEST_CSUM_SEED;
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

	ctx->es.s_inodes_count = cpu_to_le32(1024);
	ctx->es.s_first_ino = cpu_to_le32(EXT4_GOOD_OLD_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(DIR_TEST_INODE_SIZE);
	ctx->es.s_feature_incompat = cpu_to_le32(EXT4_FEATURE_INCOMPAT_FILETYPE);
	ctx->es.s_feature_compat = cpu_to_le32(EXT4_FEATURE_COMPAT_DIR_INDEX);
	ctx->es.s_feature_ro_compat =
		cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = DIR_TEST_BLK_SIZE;
	ctx->sb_bh.b_blocknr = 1;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);

	for (i = 0; i < DIR_TEST_MAX_BLOCKS; i++) {
		struct buffer_head *bh = &ctx->blocks[i].bh;

		bh->b_data = ctx->blocks[i].data;
		bh->b_size = DIR_TEST_BLK_SIZE;
		bh->b_blocknr = DIR_TEST_FIRST_PBLK + i;
		atomic_set(&bh->b_count, 1);
		set_buffer_uptodate(bh);
	}

	dir = &ctx->dir_ei.vfs_inode;
	dir->i_sb = &ctx->sb;
	dir->i_ino = DIR_TEST_DIR_INO;
	dir->i_mode = S_IFDIR | 0755;
	dir->i_blkbits = DIR_TEST_BLK_BITS;
	dir->i_mapping = &dir->i_data;
	dir->i_data.host = dir;
	set_nlink(dir, 2);
	atomic_set(&dir->i_count, 1);
	spin_lock_init(&dir->i_lock);
	i_size_ordered_init(dir);
	init_rwsem(&dir->i_rwsem);
	INIT_LIST_HEAD(&dir->i_io_list);
	INIT_LIST_HEAD(&dir->i_wb_list);
	INIT_LIST_HEAD(&dir->i_lru);
	INIT_HLIST_NODE(&dir->i_hash);
	inode_state_set_raw(dir, I_DIRTY);
	inode_set_iversion_raw(dir, 10);

	init_rwsem(&ctx->dir_ei.xattr_sem);
	init_rwsem(&ctx->dir_ei.i_data_sem);
	INIT_LIST_HEAD(&ctx->dir_ei.i_orphan);
	INIT_LIST_HEAD(&ctx->dir_ei.i_fc_list);
	spin_lock_init(&ctx->dir_ei.i_fc_lock);
	spin_lock_init(&ctx->dir_ei.i_raw_lock);
	ctx->dir_ei.i_extra_isize = DIR_TEST_EXTRA_ISIZE;
	ctx->dir_ei.i_csum_seed = DIR_TEST_CSUM_SEED ^ DIR_TEST_DIR_INO;

	init_mock_dir_file(ctx, FMODE_64BITHASH);

	kunit_activate_static_stub(test, ext4_map_blocks, dir_map_blocks_stub);
	kunit_activate_static_stub(test, ext4_getblk, dir_getblk_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   dir_dirty_metadata_stub);

	return 0;
}

/*
 * Test 1: Validate __ext4_check_dir_entry() and ext4_check_all_de() across
 * valid directory blocks (including fake '.' / '..' / csum tail entries and
 * casefold+encrypted extended-hash entries) and every corruption condition.
 */
static void test_check_dir_entry_and_all_de(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	unsigned int tail_len = sizeof(struct ext4_dir_entry_tail);
	char *buf;

	bh = alloc_dir_lblk(ctx, 0);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	buf = bh->b_data;

	/* Valid block: '.', '..', 'alpha', and a 12-byte csum tail */
	write_de(buf, 0, DIR_TEST_DIR_INO, 12, EXT4_FT_DIR, ".");
	write_de(buf, 12, DIR_TEST_ROOT_INO, 12, EXT4_FT_DIR, "..");
	write_de(buf, 24, 15, DIR_TEST_BLK_SIZE - 24 - tail_len,
		 EXT4_FT_REG_FILE, "alpha");
	ext4_initialize_dirent_tail(bh, DIR_TEST_BLK_SIZE);

	KUNIT_EXPECT_EQ(test, ext4_check_all_de(dir, bh, buf,
						DIR_TEST_BLK_SIZE), 0);

	/* 1. rec_len smaller than minimal (8 < 12) */
	de = (struct ext4_dir_entry_2 *)(buf + 24);
	de->rec_len = cpu_to_le16(8);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     &ctx->filp, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);
	KUNIT_EXPECT_EQ(test, ext4_check_all_de(dir, bh, buf,
						DIR_TEST_BLK_SIZE),
			-EFSCORRUPTED);

	/* 2. rec_len not a multiple of 4 (14 % 4 != 0) */
	de->rec_len = cpu_to_le16(14);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);

	/* 3. rec_len too small for name_len (name_len=10 needs 20 bytes) */
	de->name_len = 10;
	de->rec_len = cpu_to_le16(16);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);

	/*
	 * 4. Casefolded + encrypted directory requires space for
	 * ext4_dir_entry_hash (+8 bytes) on regular entries, while fake
	 * entries ('.', '..', csum tail) remain 12 bytes.
	 */
	dir->i_flags |= S_CASEFOLD | S_ENCRYPTED;
	write_de(buf, 24, 15, 16, EXT4_FT_REG_FILE, "ab");
	de = (struct ext4_dir_entry_2 *)(buf + 24);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);
	write_de(buf, 24, 15, DIR_TEST_BLK_SIZE - 24 - tail_len,
		 EXT4_FT_REG_FILE, "ab");
	ext4_initialize_dirent_tail(bh, DIR_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, ext4_check_all_de(dir, bh, buf,
						DIR_TEST_BLK_SIZE), 0);
	dir->i_flags &= ~(S_CASEFOLD | S_ENCRYPTED);

	/* 5. Directory entry overrun (next_offset > size) */
	de = (struct ext4_dir_entry_2 *)(buf + 24);
	de->rec_len = cpu_to_le16(DIR_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);

	/* 6. Entry too close to block end (leaves only 8 bytes < 12) */
	de->rec_len = cpu_to_le16(DIR_TEST_BLK_SIZE - 24 - 8);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);

	/* 7. Inode number out of bounds (> s_inodes_count) */
	de->rec_len = cpu_to_le16(DIR_TEST_BLK_SIZE - 24 - tail_len);
	de->inode = cpu_to_le32(2048);
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 24), 1);

	/* 8. '.' directory entry cannot span to the end of the block */
	write_de(buf, 0, DIR_TEST_DIR_INO, DIR_TEST_BLK_SIZE, EXT4_FT_DIR, ".");
	de = (struct ext4_dir_entry_2 *)buf;
	KUNIT_EXPECT_EQ(test, __ext4_check_dir_entry(__func__, __LINE__, dir,
						     NULL, de, bh, buf,
						     DIR_TEST_BLK_SIZE, 0), 1);
}

/*
 * Test 2: Verify is_dx_dir(), get_dtype(), and 32-bit / 64-bit HTree telldir
 * hash-position cookie helpers.
 */
static void test_dx_detection_and_hash_pos_cookies(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	__u32 major = 0xaabbccdeU;
	__u32 minor = 0x12345678U;
	loff_t pos;

	/* Without DIR_INDEX feature, is_dx_dir() is always 0 */
	ext4_clear_feature_dir_index(&ctx->sb);
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, DIR_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, ext4_is_dx_dir_test(dir), 0);

	/* With DIR_INDEX: multi-block dir without INDEX or inline is non-dx */
	ext4_set_feature_dir_index(&ctx->sb);
	ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, 2 * DIR_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, ext4_is_dx_dir_test(dir), 0);

	/* Single-block dir with DIR_INDEX is treated as dx-capable */
	i_size_write(dir, DIR_TEST_BLK_SIZE);
	KUNIT_EXPECT_EQ(test, ext4_is_dx_dir_test(dir), 1);

	/* Multi-block dir with EXT4_INODE_INDEX is dx */
	i_size_write(dir, 4 * DIR_TEST_BLK_SIZE);
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	KUNIT_EXPECT_EQ(test, ext4_is_dx_dir_test(dir), 1);
	ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);

	/* Inline-data dir with DIR_INDEX is treated as dx */
	ext4_set_inode_flag(dir, EXT4_INODE_INLINE_DATA);
	ctx->dir_ei.i_inline_off = 160;
	KUNIT_EXPECT_EQ(test, ext4_is_dx_dir_test(dir), 1);
	ext4_clear_inode_flag(dir, EXT4_INODE_INLINE_DATA);
	ctx->dir_ei.i_inline_off = 0;

	/* get_dtype() mapping with and without FILETYPE feature */
	KUNIT_EXPECT_EQ(test, get_dtype(&ctx->sb, EXT4_FT_REG_FILE), DT_REG);
	KUNIT_EXPECT_EQ(test, get_dtype(&ctx->sb, EXT4_FT_DIR), DT_DIR);
	KUNIT_EXPECT_EQ(test, get_dtype(&ctx->sb, EXT4_FT_SYMLINK), DT_LNK);
	KUNIT_EXPECT_EQ(test, get_dtype(&ctx->sb, EXT4_FT_DIR_CSUM),
			DT_UNKNOWN);
	ext4_clear_feature_filetype(&ctx->sb);
	KUNIT_EXPECT_EQ(test, get_dtype(&ctx->sb, EXT4_FT_REG_FILE),
			DT_UNKNOWN);
	ext4_set_feature_filetype(&ctx->sb);

	/* 64-bit hash cookie round-trip */
	ctx->filp.f_mode = FMODE_READ | FMODE_64BITHASH;
	KUNIT_EXPECT_EQ(test, ext4_get_htree_eof_test(&ctx->filp),
			(loff_t)EXT4_HTREE_EOF_64BIT);
	pos = ext4_dir_hash2pos_test(&ctx->filp, major, minor);
	KUNIT_EXPECT_EQ(test, ext4_dir_pos2maj_hash_test(&ctx->filp, pos),
			major & ~1U);
	KUNIT_EXPECT_EQ(test, ext4_dir_pos2min_hash_test(&ctx->filp, pos),
			minor);

	/* 32-bit hash cookie round-trip */
	ctx->filp.f_mode = FMODE_READ | FMODE_32BITHASH;
	KUNIT_EXPECT_EQ(test, ext4_get_htree_eof_test(&ctx->filp),
			(loff_t)EXT4_HTREE_EOF_32BIT);
	pos = ext4_dir_hash2pos_test(&ctx->filp, major, minor);
	KUNIT_EXPECT_EQ(test, pos, (loff_t)(major >> 1));
	KUNIT_EXPECT_EQ(test, ext4_dir_pos2maj_hash_test(&ctx->filp, pos),
			major & ~1U);
	KUNIT_EXPECT_EQ(test, ext4_dir_pos2min_hash_test(&ctx->filp, pos), 0U);
}

/*
 * Test 3: Verify ext4_dir_operations open(), llseek(), and release() across
 * 64-bit dx dirs, 32-bit dx dirs, and non-dx indirect/extent linear dirs.
 */
static void test_dir_open_llseek_and_release(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	struct dir_private_info *info;
	loff_t ret;

	KUNIT_ASSERT_EQ(test, ext4_dir_operations.open(dir, &ctx->filp), 0);
	info = ctx->filp.private_data;
	KUNIT_ASSERT_NOT_NULL(test, info);

	/* 1. DX directory with 64-bit hash cookies */
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, 2 * DIR_TEST_BLK_SIZE);
	ctx->filp.f_mode = FMODE_READ | FMODE_LSEEK | FMODE_64BITHASH;
	info->cookie = inode_query_iversion(dir);

	ret = ext4_dir_operations.llseek(&ctx->filp, EXT4_HTREE_EOF_64BIT,
					 SEEK_SET);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)EXT4_HTREE_EOF_64BIT);
	KUNIT_EXPECT_FALSE(test, inode_eq_iversion(dir, info->cookie));

	ret = ext4_dir_operations.llseek(&ctx->filp, -1, SEEK_SET);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)-EINVAL);

	/* 2. DX directory with 32-bit hash cookies caps at 32-bit EOF */
	ctx->filp.f_mode = FMODE_READ | FMODE_LSEEK | FMODE_32BITHASH;
	ret = ext4_dir_operations.llseek(&ctx->filp, EXT4_HTREE_EOF_32BIT,
					 SEEK_SET);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)EXT4_HTREE_EOF_32BIT);
	ret = ext4_dir_operations.llseek(&ctx->filp,
					 (loff_t)EXT4_HTREE_EOF_32BIT + 1,
					 SEEK_SET);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)-EINVAL);

	/* 3. Non-DX indirect directory uses s_bitmap_maxbytes and i_size */
	ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);
	ext4_clear_inode_flag(dir, EXT4_INODE_EXTENTS);
	i_size_write(dir, 3 * DIR_TEST_BLK_SIZE);
	info->cookie = inode_query_iversion(dir);

	ret = ext4_dir_operations.llseek(&ctx->filp, 0, SEEK_END);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)(3 * DIR_TEST_BLK_SIZE));
	KUNIT_EXPECT_FALSE(test, inode_eq_iversion(dir, info->cookie));

	ret = ext4_dir_operations.llseek(&ctx->filp,
					 ctx->sbi.s_bitmap_maxbytes + 1,
					 SEEK_SET);
	KUNIT_EXPECT_EQ(test, ret, (loff_t)-EINVAL);

	KUNIT_EXPECT_EQ(test, ext4_dir_operations.release(dir, &ctx->filp), 0);
	ctx->filp.private_data = NULL;
}

/*
 * Test 4: Verify ext4_htree_store_dirent() rbtree sorting across major and
 * minor hashes, exact 64-bit hash collision chaining, and cleanup via
 * ext4_htree_free_dir_info().
 */
static void test_htree_store_dirent_and_free(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	struct dir_private_info *info;
	int err;

	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, 2 * DIR_TEST_BLK_SIZE);
	KUNIT_ASSERT_EQ(test, ext4_dir_operations.open(dir, &ctx->filp), 0);
	info = ctx->filp.private_data;

	/* Insert out of hash order, including 3 colliding entries at 0x200:7 */
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x400, 0x10, 34,
						EXT4_FT_REG_FILE, "fourth"), 0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x100, 0x20, 31,
						EXT4_FT_DIR, "first"), 0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x400, 0x05, 33,
						EXT4_FT_SYMLINK, "third"), 0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x200, 0x07, 32,
						EXT4_FT_REG_FILE, "c1"), 0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x200, 0x07, 35,
						EXT4_FT_REG_FILE, "c2"), 0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x200, 0x07, 36,
						EXT4_FT_REG_FILE, "c3"), 0);

	/* Pre-prime cookie and next_hash so ext4_dx_readdir drains info->root */
	info->cookie = inode_query_iversion(dir);
	info->next_hash = ~0U;
	reset_capture(&ctx->cap, 0, 0);

	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 6);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "first");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[0].d_type, (unsigned int)DT_DIR);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "c1");
	/* Collision list inserts after head: c1 -> c3 -> c2 */
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[2].name, "c3");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[3].name, "c2");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[4].name, "third");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[4].d_type, (unsigned int)DT_LNK);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[5].name, "fourth");
	KUNIT_EXPECT_EQ(test, ctx->cap.ctx.pos, (loff_t)EXT4_HTREE_EOF_64BIT);

	/* Re-populate nodes so release exercises non-empty free_rb_tree_fname */
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x300, 0x01, 40,
						EXT4_FT_REG_FILE, "leftover1"),
			0);
	KUNIT_ASSERT_EQ(test, store_stub_dirent(&ctx->filp, 0x300, 0x01, 41,
						EXT4_FT_REG_FILE, "leftover2"),
			0);
	KUNIT_EXPECT_EQ(test, ext4_dir_operations.release(dir, &ctx->filp), 0);
	ctx->filp.private_data = NULL;
}

/*
 * Test 5: Verify linear ext4_readdir() across multi-block directories with
 * sparse holes, deleted entries, dir_emit() stop/resume, i_version change
 * mid-entry revalidation, checksum/corruption block skipping, and
 * ERR_BAD_DX_DIR fallback clearing EXT4_INODE_INDEX.
 */
static void test_linear_readdir(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	struct buffer_head *bh0, *bh2, *bh3;
	unsigned int tail_len = sizeof(struct ext4_dir_entry_tail);
	loff_t resume_pos;
	int err;

	/*
	 * 4-block non-indexed linear directory:
	 *   lblk 0: '.', '..', deleted (ino=0), 'file_a', 'sym_b' + csum tail
	 *   lblk 1: sparse hole (unmapped)
	 *   lblk 2: 'sub_c', 'fifo_d' + csum tail
	 *   lblk 3: 'last_e' + csum tail
	 */
	ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, 4 * DIR_TEST_BLK_SIZE);
	dir->i_blocks = (4 * DIR_TEST_BLK_SIZE) >> 9;

	bh0 = alloc_dir_lblk(ctx, 0);
	bh2 = alloc_dir_lblk(ctx, 2);
	bh3 = alloc_dir_lblk(ctx, 3);
	KUNIT_ASSERT_NOT_NULL(test, bh0);
	KUNIT_ASSERT_NOT_NULL(test, bh2);
	KUNIT_ASSERT_NOT_NULL(test, bh3);

	write_de(bh0->b_data, 0, DIR_TEST_DIR_INO, 12, EXT4_FT_DIR, ".");
	write_de(bh0->b_data, 12, DIR_TEST_ROOT_INO, 12, EXT4_FT_DIR, "..");
	write_de(bh0->b_data, 24, 0, 16, EXT4_FT_REG_FILE, "deleted");
	write_de(bh0->b_data, 40, 51, 20, EXT4_FT_REG_FILE, "file_a");
	write_de(bh0->b_data, 60, 52, DIR_TEST_BLK_SIZE - 60 - tail_len,
		 EXT4_FT_SYMLINK, "sym_b");
	ext4_initialize_dirent_tail(bh0, DIR_TEST_BLK_SIZE);
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh0), 0);

	write_de(bh2->b_data, 0, 53, 16, EXT4_FT_DIR, "sub_c");
	write_de(bh2->b_data, 16, 54, DIR_TEST_BLK_SIZE - 16 - tail_len,
		 EXT4_FT_FIFO, "fifo_d");
	ext4_initialize_dirent_tail(bh2, DIR_TEST_BLK_SIZE);
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh2), 0);

	write_de(bh3->b_data, 0, 55, DIR_TEST_BLK_SIZE - tail_len,
		 EXT4_FT_REG_FILE, "last_e");
	ext4_initialize_dirent_tail(bh3, DIR_TEST_BLK_SIZE);
	KUNIT_ASSERT_EQ(test, ext4_handle_dirty_dirblock(NULL, dir, bh3), 0);

	KUNIT_ASSERT_EQ(test, ext4_dir_operations.open(dir, &ctx->filp), 0);

	/* 1. Stop after 3 emitted entries ('.', '..', 'file_a'), then resume */
	reset_capture(&ctx->cap, 0, 3);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 3);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, ".");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "..");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[2].name, "file_a");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[2].d_type, (unsigned int)DT_REG);

	resume_pos = ctx->cap.ctx.pos;
	KUNIT_EXPECT_EQ(test, resume_pos, (loff_t)60);

	reset_capture(&ctx->cap, resume_pos, 0);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 4);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "sym_b");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[0].d_type, (unsigned int)DT_LNK);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "sub_c");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[1].d_type, (unsigned int)DT_DIR);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[2].name, "fifo_d");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[2].d_type, (unsigned int)DT_FIFO);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[3].name, "last_e");

	/*
	 * 2. Unaligned ctx->pos (44, inside 'file_a' at [40..60)) combined
	 * with an i_version bump triggers block-start rescan and snaps to 60.
	 */
	inode_inc_iversion(dir);
	reset_capture(&ctx->cap, 44, 1);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 1);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "sym_b");
	KUNIT_EXPECT_EQ(test, ctx->cap.recs[0].offset, (loff_t)60);

	/*
	 * 3. Corrupt block 0 checksum and corrupt second entry in block 2:
	 * ext4_readdir() skips block 0, emits 'sub_c' from block 2 before
	 * skipping the rest of block 2, and still emits 'last_e' from block 3.
	 */
	clear_buffer_verified(bh0);
	bh0->b_data[DIR_TEST_BLK_SIZE - 1] ^= 0xff;
	((struct ext4_dir_entry_2 *)(bh2->b_data + 16))->rec_len =
		cpu_to_le16(8);

	reset_capture(&ctx->cap, 0, 0);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 2);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "sub_c");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "last_e");

	/*
	 * 4. ERR_BAD_DX_DIR fallback when !metadata_csum clears
	 * EXT4_INODE_INDEX and falls back to linear block scanning.
	 */
	ext4_clear_feature_metadata_csum(&ctx->sb);
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	write_de(bh3->b_data, 0, 55, DIR_TEST_BLK_SIZE,
		 EXT4_FT_REG_FILE, "last_e");
	kunit_activate_static_stub(test, ext4_htree_fill_tree,
				   dir_htree_fill_tree_stub);
	ctx->htree_mode = DIR_HTREE_STUB_BAD_DX;

	reset_capture(&ctx->cap, 3 * DIR_TEST_BLK_SIZE, 0);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, ext4_test_inode_flag(dir, EXT4_INODE_INDEX));
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 1);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "last_e");

	KUNIT_EXPECT_EQ(test, ext4_dir_operations.release(dir, &ctx->filp), 0);
	ctx->filp.private_data = NULL;
}

/*
 * Test 6: Verify ext4_dx_readdir() across multi-batch leaf refills,
 * mid-collision-chain stop & extra_fname resumption, EOF short-circuit,
 * f_pos seek tree reset, and mid-stream i_version invalidation.
 */
static void test_dx_readdir(struct kunit *test)
{
	struct dir_test_ctx *ctx = test->priv;
	struct inode *dir = &ctx->dir_ei.vfs_inode;
	loff_t saved_pos, seek_pos;
	int err;

	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	i_size_write(dir, 2 * DIR_TEST_BLK_SIZE);
	kunit_activate_static_stub(test, ext4_htree_fill_tree,
				   dir_htree_fill_tree_stub);
	ctx->htree_mode = DIR_HTREE_STUB_TWO_BATCH;

	KUNIT_ASSERT_EQ(test, ext4_dir_operations.open(dir, &ctx->filp), 0);

	/*
	 * 1. Stop after 3 entries ('.', '..', and first collision entry):
	 * leaves the second colliding entry queued on info->extra_fname.
	 */
	reset_capture(&ctx->cap, 0, 3);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->htree_fill_calls, 1);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 3);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, ".");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "..");
	/* Collision chain at 0x1000:0x55 emits coll_a then coll_b */
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[2].name, "coll_a");
	saved_pos = ctx->cap.ctx.pos;

	/*
	 * 2. Resume from saved_pos: emits leftover 'coll_b' from extra_fname
	 * without refilling batch 1, then refills at next_hash=0x2000 for
	 * 'leaf2_sub', and finishes at EXT4_HTREE_EOF_64BIT.
	 */
	reset_capture(&ctx->cap, saved_pos, 0);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->htree_fill_calls, 2);
	KUNIT_EXPECT_EQ(test, ctx->last_fill_start_hash, 0x2000U);
	KUNIT_ASSERT_EQ(test, ctx->cap.count, 2);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "coll_b");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "leaf2_sub");
	KUNIT_EXPECT_EQ(test, ctx->cap.ctx.pos, (loff_t)EXT4_HTREE_EOF_64BIT);

	/* 3. Calling again at HTREE_EOF returns 0 immediately without refill */
	reset_capture(&ctx->cap, ctx->cap.ctx.pos, 0);
	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->cap.count, 0);
	KUNIT_EXPECT_EQ(test, ctx->htree_fill_calls, 2);

	/*
	 * 4. Seeking to a 64-bit hash cookie resets the cached state and
	 * refills starting from (pos2maj_hash, pos2min_hash); bumping
	 * i_version after the first entry forces an rbtree refill mid-walk.
	 */
	seek_pos = ext4_dir_hash2pos_test(&ctx->filp, 0x1000, 0x55);
	reset_capture(&ctx->cap, seek_pos, 0);
	ctx->cap.bump_iversion_after_first = dir;

	inode_lock_shared(dir);
	err = ext4_dir_operations.iterate_shared(&ctx->filp, &ctx->cap.ctx);
	inode_unlock_shared(dir);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_GE(test, ctx->htree_fill_calls, 4);
	KUNIT_EXPECT_GE(test, ctx->cap.count, 3);
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[0].name, "coll_a");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[1].name, "coll_b");
	KUNIT_EXPECT_STREQ(test, ctx->cap.recs[ctx->cap.count - 1].name,
			   "leaf2_sub");

	KUNIT_EXPECT_EQ(test, ext4_dir_operations.release(dir, &ctx->filp), 0);
	ctx->filp.private_data = NULL;
}

static struct kunit_case ext4_dir_test_cases[] = {
	KUNIT_CASE(test_check_dir_entry_and_all_de),
	KUNIT_CASE(test_dx_detection_and_hash_pos_cookies),
	KUNIT_CASE(test_dir_open_llseek_and_release),
	KUNIT_CASE(test_htree_store_dirent_and_free),
	KUNIT_CASE(test_linear_readdir),
	KUNIT_CASE(test_dx_readdir),
	{}
};

static struct kunit_suite ext4_dir_test_suite = {
	.name = "ext4_dir_test",
	.init = dir_test_init,
	.test_cases = ext4_dir_test_cases,
};

kunit_test_suites(&ext4_dir_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 directory operations");
MODULE_LICENSE("GPL");
