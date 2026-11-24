// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/orphan.c
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/crc32c.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/quotaops.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "xattr.h"

/*
 * Mock filesystem geometry:
 *  - 2 block groups, 1024-byte blocks (254 orphan entries per orphan block)
 *  - 64 inodes per group (128 total inodes), 256-byte inodes
 *  - s_first_ino = 11, s_orphan_file_inum = 12
 */
#define ORPHAN_TEST_NGROUPS		2U
#define ORPHAN_TEST_BLK_SIZE		1024U
#define ORPHAN_TEST_BLK_BITS		10U
#define ORPHAN_TEST_BLOCKS_PER_GRP	1024U
#define ORPHAN_TEST_FIRST_DATA_BLK	1U
#define ORPHAN_TEST_TOTAL_BLKS		(ORPHAN_TEST_FIRST_DATA_BLK + \
					 ORPHAN_TEST_NGROUPS * \
					 ORPHAN_TEST_BLOCKS_PER_GRP)
#define ORPHAN_TEST_INODE_SIZE		256U
#define ORPHAN_TEST_EXTRA_ISIZE		32U
#define ORPHAN_TEST_INODES_PER_BLK	(ORPHAN_TEST_BLK_SIZE / \
					 ORPHAN_TEST_INODE_SIZE)
#define ORPHAN_TEST_INODES_PER_GRP	64U
#define ORPHAN_TEST_ITB_PER_GRP		(ORPHAN_TEST_INODES_PER_GRP / \
					 ORPHAN_TEST_INODES_PER_BLK)
#define ORPHAN_TEST_TOTAL_INODES	(ORPHAN_TEST_NGROUPS * \
					 ORPHAN_TEST_INODES_PER_GRP)
#define ORPHAN_TEST_FIRST_INO		11U
#define ORPHAN_TEST_ORPHAN_INO		12U
#define ORPHAN_TEST_CSUM_SEED		0x4d7a19c5U
#define ORPHAN_TEST_OF_CSUM_SEED	0x91e23b77U
#define ORPHAN_TEST_INODES_PER_OB	254U

#define ORPHAN_TEST_GDESC_BLK		2ULL
#define ORPHAN_TEST_OB_BASE_BLK		500ULL
#define ORPHAN_TEST_MAX_OB_BLKS		4U
#define ORPHAN_TEST_MAX_SLOTS		16U

struct orphan_test_ibitmap {
	struct buffer_head bh;
	char buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);
};

struct orphan_test_ob_blk {
	bool present;
	int bread_err;
	bool return_hole;
	struct buffer_head bh;
	char buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);
};

struct orphan_test_inode_slot {
	bool in_use;
	unsigned long ino;
	bool handoff_to_iget;
	struct inode *inode;
	struct buffer_head iloc_bh;
	char raw_buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);
};

struct orphan_test_ctx {
	struct super_block *sb;
	struct ext4_sb_info sbi;
	struct ext4_super_block es;
	struct blockgroup_lock bgl;
	struct gendisk bdev_disk;
	struct block_device bdev;
	struct request_queue bdev_queue;
	struct inode bd_inode;
	struct buffer_head sb_bh;
	struct buffer_head gdesc_bh;
	char gdesc_buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);

	struct ext4_group_desc gdps[ORPHAN_TEST_NGROUPS];
	struct ext4_group_info *grp_infos[ORPHAN_TEST_NGROUPS];
	struct ext4_group_info *grp_info_ptrs[16];
	struct ext4_group_info **grp_info_slice[1];
	struct orphan_test_ibitmap ibitmaps[ORPHAN_TEST_NGROUPS];

	journal_t mock_journal;
	handle_t mock_handle;

	struct orphan_test_ob_blk ob_pool[ORPHAN_TEST_MAX_OB_BLKS];
	struct ext4_orphan_block static_binfo[ORPHAN_TEST_MAX_OB_BLKS];

	/* Shared read-only block used for 512-block max-size boundary test */
	bool use_boundary_bh;
	struct buffer_head boundary_bh;
	char boundary_buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);
	char frozen_buf[ORPHAN_TEST_BLK_SIZE] __aligned(8);
#ifdef CONFIG_QUOTA
	char qf_name[16];
#endif

	struct orphan_test_inode_slot slots[ORPHAN_TEST_MAX_SLOTS];
	bool orphan_info_allocated;
	bool enable_real_evict;

	/* Fault injection controls */
	int fail_write_access_err;
	int fail_write_access_on_call;
	int fail_dirty_metadata_err;
	int fail_dirty_metadata_on_call;
	int fail_get_iloc_err;
	int fail_get_iloc_on_call;
	int fail_mark_iloc_err;
	int fail_mark_iloc_on_call;
	int fail_mark_inode_dirty_err;
	int fail_iget_err;
	unsigned long fail_iget_ino;
	int stub_truncate_err;

	/* Call tracking & verification state */
	unsigned int write_access_calls;
	enum ext4_journal_trigger_type last_trigger_type;
	unsigned int dirty_metadata_calls;
	unsigned int get_iloc_calls;
	unsigned int mark_iloc_calls;
	unsigned int iget_calls;
	unsigned int truncate_calls;
	bool saw_rw_during_cleanup;

	unsigned long freed_inos[ORPHAN_TEST_MAX_SLOTS];
	unsigned int nr_freed_inos;
	ext4_fsblk_t last_freed_blk;
	unsigned long total_freed_blks;
};

static inline ext4_fsblk_t orphan_test_ibitmap_blk(ext4_group_t g)
{
	return ORPHAN_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * ORPHAN_TEST_BLOCKS_PER_GRP + 3ULL;
}

static inline ext4_fsblk_t orphan_test_bbitmap_blk(ext4_group_t g)
{
	return ORPHAN_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * ORPHAN_TEST_BLOCKS_PER_GRP + 2ULL;
}

static inline ext4_fsblk_t orphan_test_itable_blk(ext4_group_t g)
{
	return ORPHAN_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * ORPHAN_TEST_BLOCKS_PER_GRP + 4ULL;
}

static void orphan_test_sb_upd_work(struct work_struct *work)
{
}

static void orphan_test_err_timer(struct timer_list *t)
{
}

static void orphan_test_init_ei_fields(struct ext4_inode_info *ei)
{
	inode_set_iversion(&ei->vfs_inode, 1);
	ei->i_flags = 0;
	ext4_clear_state_flags(ei);
	spin_lock_init(&ei->i_raw_lock);
	ei->i_prealloc_node = RB_ROOT;
	atomic_set(&ei->i_prealloc_active, 0);
	rwlock_init(&ei->i_prealloc_lock);
	ext4_es_init_tree(&ei->i_es_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_list);
	ei->i_es_all_nr = 0;
	ei->i_es_shk_nr = 0;
	ei->i_es_shrink_lblk = 0;
	ei->i_es_seq = 0;
	ei->i_reserved_data_blocks = 0;
	spin_lock_init(&ei->i_block_reservation_lock);
	ext4_init_pending_tree(&ei->i_pending_tree);
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
	memset(&ei->i_dquot, 0, sizeof(ei->i_dquot));
#endif
#ifdef CONFIG_FS_ENCRYPTION
	ei->i_crypt_info = NULL;
#endif
	ei->jinode = NULL;
	init_rwsem(&ei->xattr_sem);
	INIT_LIST_HEAD(&ei->i_rsv_conversion_list);
	spin_lock_init(&ei->i_completed_io_lock);
	ei->i_sync_tid = 0;
	ei->i_datasync_tid = 0;
	INIT_LIST_HEAD(&ei->i_orphan);
	ext4_fc_init_inode(&ei->vfs_inode);
	spin_lock_init(&ei->i_fc_lock);
	ei->i_metadata_bhs = NULL;
	ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);
	ei->i_extra_isize = ORPHAN_TEST_EXTRA_ISIZE;
	ei->i_csum_seed = ORPHAN_TEST_OF_CSUM_SEED;
	ei->i_last_alloc_group = ~0U;
}

static struct inode *orphan_test_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	orphan_test_init_ei_fields(ei);
	return &ei->vfs_inode;
}

static void orphan_test_evict_inode(struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (ctx && ctx->enable_real_evict) {
		ext4_evict_inode(inode);
		return;
	}
	truncate_inode_pages_final(&inode->i_data);
	ext4_clear_inode(inode);
}

static void orphan_test_destroy_inode(struct inode *inode)
{
	kfree(ext4_i_metadata_bhs(inode));
	EXT4_I(inode)->i_metadata_bhs = NULL;
}

static void orphan_test_free_inode_mem(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **orphan_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *orphan_test_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations orphan_test_qops = {
	.get_reserved_space = orphan_test_get_reserved_space,
	.alloc_dquot	    = dquot_alloc,
	.destroy_dquot	    = dquot_destroy,
	.get_next_id	    = dquot_get_next_id,
};
#endif

static const struct super_operations orphan_test_sops = {
	.alloc_inode	= orphan_test_alloc_inode,
	.evict_inode	= orphan_test_evict_inode,
	.destroy_inode	= orphan_test_destroy_inode,
	.free_inode	= orphan_test_free_inode_mem,
	.drop_inode	= inode_just_drop,
#ifdef CONFIG_QUOTA
	.get_dquots	= orphan_test_get_dquots,
#endif
};

static int orphan_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type orphan_test_fs_type = {
	.name		 = "ext4_orphan_kunit",
	.init_fs_context = orphan_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

/* --- Slot & Orphan Block Helpers --- */

static struct orphan_test_inode_slot *
orphan_test_find_slot(struct orphan_test_ctx *ctx, unsigned long ino)
{
	unsigned int i;

	for (i = 0; i < ORPHAN_TEST_MAX_SLOTS; i++) {
		if (ctx->slots[i].in_use && ctx->slots[i].ino == ino)
			return &ctx->slots[i];
	}
	return NULL;
}

static void orphan_test_set_ibitmap_bit(struct orphan_test_ctx *ctx,
					unsigned long ino, bool set)
{
	ext4_group_t g = (ino - 1) / ORPHAN_TEST_INODES_PER_GRP;
	unsigned int bit = (ino - 1) % ORPHAN_TEST_INODES_PER_GRP;

	if (g >= ORPHAN_TEST_NGROUPS)
		return;
	if (set)
		ext4_set_bit(bit, ctx->ibitmaps[g].buf);
	else
		ext4_clear_bit(bit, ctx->ibitmaps[g].buf);
}

static struct orphan_test_inode_slot *
orphan_test_create_inode(struct kunit *test, struct orphan_test_ctx *ctx,
			 unsigned long ino, umode_t mode, unsigned int nlink)
{
	struct orphan_test_inode_slot *slot = NULL;
	struct ext4_extent_header *eh;
	struct inode *inode;
	unsigned int i;

	for (i = 0; i < ORPHAN_TEST_MAX_SLOTS; i++) {
		if (!ctx->slots[i].in_use) {
			slot = &ctx->slots[i];
			break;
		}
	}
	KUNIT_ASSERT_NOT_NULL(test, slot);

	inode = new_inode(ctx->sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);

	memset(slot, 0, sizeof(*slot));
	slot->in_use = true;
	slot->ino = ino;
	slot->inode = inode;
	slot->iloc_bh.b_data = slot->raw_buf;
	slot->iloc_bh.b_size = ORPHAN_TEST_BLK_SIZE;
	slot->iloc_bh.b_bdev = &ctx->bdev;
	slot->iloc_bh.b_blocknr = orphan_test_itable_blk(0) + i;
	atomic_set(&slot->iloc_bh.b_count, 1);
	set_buffer_uptodate(&slot->iloc_bh);

	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = ctx->sb->s_blocksize_bits;
	inode->i_flags |= S_NOQUOTA;
	set_nlink(inode, nlink);
	EXT4_I(inode)->i_block_group = (ino - 1) / ORPHAN_TEST_INODES_PER_GRP;

	if (S_ISREG(mode) || S_ISDIR(mode)) {
		ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
		eh = ext_inode_hdr(inode);
		memset(eh, 0, sizeof(EXT4_I(inode)->i_data));
		eh->eh_magic = EXT4_EXT_MAGIC;
		eh->eh_entries = 0;
		eh->eh_max = cpu_to_le16((sizeof(EXT4_I(inode)->i_data) -
					  sizeof(*eh)) /
					 sizeof(struct ext4_extent));
		eh->eh_depth = 0;
	}

	orphan_test_set_ibitmap_bit(ctx, ino, true);
	return slot;
}

static void orphan_test_add_single_extent(struct inode *inode,
					  ext4_lblk_t lblk, ext4_fsblk_t pblk,
					  unsigned short len)
{
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex = EXT_FIRST_EXTENT(eh);

	eh->eh_entries = cpu_to_le16(1);
	ex->ee_block = cpu_to_le32(lblk);
	ex->ee_len = cpu_to_le16(len);
	ext4_ext_store_pblock(ex, pblk);
	inode->i_blocks = (blkcnt_t)len << (inode->i_blkbits - 9);
}

static __u32 orphan_test_calc_ob_csum(struct super_block *sb, __u32 seed,
				      ext4_fsblk_t blocknr, const void *data)
{
	__le64 dsk_block_nr = cpu_to_le64(blocknr);
	size_t payload_bytes = sb->s_blocksize -
			       sizeof(struct ext4_orphan_block_tail);
	__u32 csum;

	csum = crc32c(seed, &dsk_block_nr, sizeof(dsk_block_nr));
	return crc32c(csum, data, payload_bytes);
}

static void orphan_test_seal_ob_bh(struct super_block *sb, __u32 seed,
				   struct buffer_head *bh, __u32 magic)
{
	struct ext4_orphan_block_tail *ot;
	__u32 csum;

	ot = (struct ext4_orphan_block_tail *)(bh->b_data + sb->s_blocksize -
					       sizeof(*ot));
	ot->ob_magic = cpu_to_le32(magic);
	csum = orphan_test_calc_ob_csum(sb, seed, bh->b_blocknr, bh->b_data);
	ot->ob_checksum = cpu_to_le32(csum);
}

static void orphan_test_setup_static_orphan_file(struct orphan_test_ctx *ctx,
						 int num_blocks)
{
	struct super_block *sb = ctx->sb;
	struct ext4_orphan_info *oi = &ctx->sbi.s_orphan_info;
	int i;

	ext4_set_feature_orphan_file(sb);
	oi->of_blocks = num_blocks;
	oi->of_csum_seed = ORPHAN_TEST_OF_CSUM_SEED;
	oi->of_binfo = ctx->static_binfo;
	ctx->orphan_info_allocated = false;

	for (i = 0; i < num_blocks; i++) {
		struct orphan_test_ob_blk *oblk = &ctx->ob_pool[i];

		memset(oblk, 0, sizeof(*oblk));
		oblk->present = true;
		oblk->bh.b_data = oblk->buf;
		oblk->bh.b_size = sb->s_blocksize;
		oblk->bh.b_bdev = &ctx->bdev;
		oblk->bh.b_blocknr = ORPHAN_TEST_OB_BASE_BLK + i;
		atomic_set(&oblk->bh.b_count, 1);
		set_buffer_uptodate(&oblk->bh);
		orphan_test_seal_ob_bh(sb, oi->of_csum_seed, &oblk->bh,
				       EXT4_ORPHAN_BLOCK_MAGIC);

		ctx->static_binfo[i].ob_bh = &oblk->bh;
		atomic_set(&ctx->static_binfo[i].ob_free_entries,
			   ORPHAN_TEST_INODES_PER_OB);
	}
}

static void orphan_test_reset_faults(struct orphan_test_ctx *ctx)
{
	ctx->fail_write_access_err = 0;
	ctx->fail_write_access_on_call = 0;
	ctx->fail_dirty_metadata_err = 0;
	ctx->fail_dirty_metadata_on_call = 0;
	ctx->fail_get_iloc_err = 0;
	ctx->fail_get_iloc_on_call = 0;
	ctx->fail_mark_iloc_err = 0;
	ctx->fail_mark_iloc_on_call = 0;
	ctx->fail_mark_inode_dirty_err = 0;
	ctx->fail_iget_err = 0;
	ctx->fail_iget_ino = 0;
	ctx->stub_truncate_err = 0;

	ctx->write_access_calls = 0;
	ctx->last_trigger_type = EXT4_JTR_NONE;
	ctx->dirty_metadata_calls = 0;
	ctx->get_iloc_calls = 0;
	ctx->mark_iloc_calls = 0;
	ctx->iget_calls = 0;
	ctx->truncate_calls = 0;
	ctx->saw_rw_during_cleanup = false;
	ctx->nr_freed_inos = 0;
	ctx->last_freed_blk = 0;
	ctx->total_freed_blks = 0;
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;
}

static bool orphan_test_was_ino_freed(const struct orphan_test_ctx *ctx,
				      unsigned long ino)
{
	unsigned int i;

	for (i = 0; i < ctx->nr_freed_inos; i++) {
		if (ctx->freed_inos[i] == ino)
			return true;
	}
	return false;
}

static int orphan_test_call_add(handle_t *handle, struct inode *inode)
{
	int err;

	inode_lock(inode);
	err = ext4_orphan_add(handle, inode);
	inode_unlock(inode);
	return err;
}

static int orphan_test_call_del(handle_t *handle, struct inode *inode)
{
	int err;

	inode_lock(inode);
	err = ext4_orphan_del(handle, inode);
	inode_unlock(inode);
	return err;
}

/* --- KUnit Static Stubs --- */

static int
orphan_test_get_write_access_stub(const char *where, unsigned int line,
				  handle_t *handle, struct super_block *sb,
				  struct buffer_head *bh,
				  enum ext4_journal_trigger_type trigger_type)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;

	ctx->write_access_calls++;
	ctx->last_trigger_type = trigger_type;
	if (ctx->fail_write_access_err &&
	    (!ctx->fail_write_access_on_call ||
	     ctx->write_access_calls == (unsigned int)ctx->fail_write_access_on_call))
		return ctx->fail_write_access_err;
	return 0;
}

static int orphan_test_dirty_metadata_stub(const char *where, unsigned int line,
					   handle_t *handle,
					   struct inode *inode,
					   struct buffer_head *bh)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;

	ctx->dirty_metadata_calls++;
	if (ctx->fail_dirty_metadata_err &&
	    (!ctx->fail_dirty_metadata_on_call ||
	     ctx->dirty_metadata_calls == (unsigned int)ctx->fail_dirty_metadata_on_call))
		return ctx->fail_dirty_metadata_err;

	if (bh) {
		set_buffer_uptodate(bh);
		clear_buffer_dirty(bh);
	}
	return 0;
}

static int orphan_test_get_inode_loc_stub(struct inode *inode,
					  struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;
	struct orphan_test_inode_slot *slot;

	if (!ctx)
		return -EIO;

	ctx->get_iloc_calls++;
	if (ctx->fail_get_iloc_err &&
	    (!ctx->fail_get_iloc_on_call ||
	     ctx->get_iloc_calls == (unsigned int)ctx->fail_get_iloc_on_call))
		return ctx->fail_get_iloc_err;

	slot = orphan_test_find_slot(ctx, inode->i_ino);
	if (!slot)
		return -ENOENT;

	get_bh(&slot->iloc_bh);
	iloc->bh = &slot->iloc_bh;
	iloc->offset = 0;
	iloc->block_group = EXT4_I(inode)->i_block_group;
	return 0;
}

static int orphan_test_mark_iloc_dirty_stub(handle_t *handle,
					    struct inode *inode,
					    struct ext4_iloc *iloc)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;
	int err = 0;

	if (ctx) {
		ctx->mark_iloc_calls++;
		if (ctx->fail_mark_iloc_err &&
		    (!ctx->fail_mark_iloc_on_call ||
		     ctx->mark_iloc_calls == (unsigned int)ctx->fail_mark_iloc_on_call))
			err = ctx->fail_mark_iloc_err;
	}

	/* Real ext4_mark_iloc_dirty() always drops iloc->bh */
	brelse(iloc->bh);
	iloc->bh = NULL;
	return err;
}

static int orphan_test_mark_inode_dirty_stub(handle_t *handle,
					     struct inode *inode,
					     const char *func,
					     unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;
	return ctx->fail_mark_inode_dirty_err;
}

static struct inode *orphan_test_iget_stub(struct super_block *sb,
					   unsigned long ino,
					   ext4_iget_flags flags,
					   const char *function,
					   unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;
	struct orphan_test_inode_slot *slot;
	struct inode *inode;

	if (!ctx)
		return ERR_PTR(-EIO);

	ctx->iget_calls++;
	if (ctx->fail_iget_err &&
	    (!ctx->fail_iget_ino || ctx->fail_iget_ino == ino))
		return ERR_PTR(ctx->fail_iget_err);

	slot = orphan_test_find_slot(ctx, ino);
	if (!slot || !slot->inode)
		return ERR_PTR(-ENOENT);

	inode = slot->inode;
	if (slot->handoff_to_iget)
		slot->inode = NULL;
	else
		ihold(inode);
	return inode;
}

static struct buffer_head *orphan_test_getblk_stub(handle_t *handle,
						   struct inode *inode,
						   ext4_lblk_t block,
						   int map_flags)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;
	struct orphan_test_ob_blk *oblk;

	if (!ctx)
		return ERR_PTR(-EIO);

	if (ctx->use_boundary_bh) {
		get_bh(&ctx->boundary_bh);
		return &ctx->boundary_bh;
	}

	if (block >= ORPHAN_TEST_MAX_OB_BLKS)
		return ERR_PTR(-EFSCORRUPTED);

	oblk = &ctx->ob_pool[block];
	if (oblk->bread_err)
		return ERR_PTR(oblk->bread_err);
	if (oblk->return_hole || !oblk->present)
		return NULL;

	get_bh(&oblk->bh);
	return &oblk->bh;
}

static struct ext4_group_desc *
orphan_test_get_group_desc_stub(struct super_block *sb,
				ext4_group_t block_group,
				struct buffer_head **bh)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx || block_group >= ORPHAN_TEST_NGROUPS)
		return NULL;
	if (bh)
		*bh = &ctx->gdesc_bh;
	return &ctx->gdps[block_group];
}

static struct buffer_head *orphan_test_sb_getblk_stub(struct super_block *sb,
						      ext4_fsblk_t block)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;
	ext4_group_t g;

	if (!ctx)
		return NULL;

	for (g = 0; g < ORPHAN_TEST_NGROUPS; g++) {
		if (block == orphan_test_ibitmap_blk(g)) {
			get_bh(&ctx->ibitmaps[g].bh);
			return &ctx->ibitmaps[g].bh;
		}
	}
	return NULL;
}

static void orphan_test_free_inode_stub(handle_t *handle, struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (ctx) {
		if (!sb_rdonly(inode->i_sb))
			ctx->saw_rw_during_cleanup = true;
		if (ctx->nr_freed_inos < ORPHAN_TEST_MAX_SLOTS)
			ctx->freed_inos[ctx->nr_freed_inos++] = inode->i_ino;
		orphan_test_set_ibitmap_bit(ctx, inode->i_ino, false);
	}
	ext4_clear_inode(inode);
}

static void orphan_test_free_blocks_stub(handle_t *handle, struct inode *inode,
					 struct buffer_head *bh,
					 ext4_fsblk_t block,
					 unsigned long count, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (bh)
		brelse(bh);
	if (!ctx)
		return;
	ctx->last_freed_blk = block;
	ctx->total_freed_blks += count;
}

static int orphan_test_truncate_stub(struct inode *inode)
{
	struct kunit *test = kunit_get_current_test();
	struct orphan_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return 0;

	ctx->truncate_calls++;
	if (!sb_rdonly(inode->i_sb))
		ctx->saw_rw_during_cleanup = true;
	return ctx->stub_truncate_err;
}

/* --- Fixture Setup & Teardown --- */

static void orphan_test_cleanup(struct orphan_test_ctx *ctx)
{
	handle_t *handle;
	unsigned int i;

	ctx->enable_real_evict = false;
	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state &= ~EXT4_ORPHAN_FS;

	while ((handle = ext4_journal_current_handle()) != NULL)
		ext4_journal_stop(handle);

	if (ctx->orphan_info_allocated) {
		ext4_release_orphan_info(ctx->sb);
		ctx->orphan_info_allocated = false;
	}
	memset(&ctx->sbi.s_orphan_info, 0, sizeof(ctx->sbi.s_orphan_info));
	INIT_LIST_HEAD(&ctx->sbi.s_orphan);

	for (i = 0; i < ORPHAN_TEST_MAX_SLOTS; i++) {
		struct inode *inode = ctx->slots[i].inode;

		if (!inode)
			continue;
		INIT_LIST_HEAD(&EXT4_I(inode)->i_orphan);
		ext4_clear_inode_state(inode, EXT4_STATE_ORPHAN_FILE);
		spin_lock(&inode->i_lock);
		inode_state_clear(inode, I_DIRTY_TIME | I_NEW);
		spin_unlock(&inode->i_lock);
		set_nlink(inode, 1);
		iput(inode);
		ctx->slots[i].inode = NULL;
	}

	deactivate_super(ctx->sb);
	rcu_barrier();

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(orphan_test_cleanup_action, orphan_test_cleanup,
			    struct orphan_test_ctx *);

static int orphan_test_init(struct kunit *test)
{
	struct orphan_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	ext4_group_t g;
	unsigned int bit;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	if (!ctx->bdev.bd_stats) {
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bd_stats");
	}

	fc = fs_context_for_mount(&orphan_test_fs_type, 0);
	if (IS_ERR(fc)) {
		err = PTR_ERR(fc);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	sb = sget_fc(fc, NULL, set_anon_super_fc);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		err = PTR_ERR(sb);
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	ctx->sb = sb;
	sbi = &ctx->sbi;

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &orphan_test_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &orphan_test_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = ORPHAN_TEST_BLK_SIZE;
	sb->s_blocksize_bits = ORPHAN_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_orphan_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = ORPHAN_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev_disk.part0 = &ctx->bdev;
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	/* Writable bdev by default so ext4_orphan_cleanup can proceed */
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(ORPHAN_TEST_FIRST_DATA_BLK);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	ctx->es.s_inodes_count = cpu_to_le32(ORPHAN_TEST_TOTAL_INODES);
	ctx->es.s_inodes_per_group = cpu_to_le32(ORPHAN_TEST_INODES_PER_GRP);
	ctx->es.s_blocks_per_group = cpu_to_le32(ORPHAN_TEST_BLOCKS_PER_GRP);
	ctx->es.s_clusters_per_group = cpu_to_le32(ORPHAN_TEST_BLOCKS_PER_GRP);
	ctx->es.s_first_ino = cpu_to_le32(ORPHAN_TEST_FIRST_INO);
	ctx->es.s_orphan_file_inum = cpu_to_le32(ORPHAN_TEST_ORPHAN_INO);
	ctx->es.s_inode_size = cpu_to_le16(ORPHAN_TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(ORPHAN_TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(ORPHAN_TEST_EXTRA_ISIZE);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_blocks_count_set(&ctx->es, ORPHAN_TEST_TOTAL_BLKS);

	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_extents(sb);
	ext4_set_feature_extra_isize(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);

	/* Keep sb_bh unmapped so ext4_commit_super() skips bh_submit I/O */
	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;
	ext4_superblock_csum_set(sb);

	ctx->gdesc_bh.b_data = ctx->gdesc_buf;
	ctx->gdesc_bh.b_size = ORPHAN_TEST_BLK_SIZE;
	ctx->gdesc_bh.b_blocknr = ORPHAN_TEST_GDESC_BLK;
	ctx->gdesc_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->gdesc_bh.b_count, 1);
	set_buffer_uptodate(&ctx->gdesc_bh);

	sbi->s_inode_size = ORPHAN_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = ORPHAN_TEST_INODES_PER_BLK;
	sbi->s_inodes_per_group = ORPHAN_TEST_INODES_PER_GRP;
	sbi->s_itb_per_group = ORPHAN_TEST_ITB_PER_GRP;
	sbi->s_first_ino = ORPHAN_TEST_FIRST_INO;
	sbi->s_want_extra_isize = ORPHAN_TEST_EXTRA_ISIZE;
	sbi->s_blocks_per_group = ORPHAN_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = ORPHAN_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = ORPHAN_TEST_NGROUPS;
	sbi->s_gdb_count = 1;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = ORPHAN_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block_bits = ilog2(sbi->s_desc_per_block);
	sbi->s_bitmap_maxbytes = 0x1ff7ffff000ULL;
	sbi->s_csum_seed = ORPHAN_TEST_CSUM_SEED;

	mutex_init(&sbi->s_orphan_lock);
	INIT_LIST_HEAD(&sbi->s_orphan);
	sbi->s_journal = &ctx->mock_journal;
	sbi->s_journal_triggers[EXT4_JTR_ORPHAN_FILE].sb = sb;
	sbi->s_journal_triggers[EXT4_JTR_ORPHAN_FILE].tr_triggers.t_frozen =
		ext4_orphan_file_block_trigger;

	set_opt(sb, ERRORS_CONT);
	set_opt(sb, ORDERED_DATA);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	INIT_WORK(&sbi->s_sb_upd_work, orphan_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, orphan_test_err_timer, 0);
	mutex_init(&sbi->s_fc_lock);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_q[FC_Q_STAGING]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_MAIN]);
	INIT_LIST_HEAD(&sbi->s_fc_dentry_q[FC_Q_STAGING]);

	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL, 0);

	for (g = 0; g < ORPHAN_TEST_NGROUPS; g++) {
		struct orphan_test_ibitmap *ibm = &ctx->ibitmaps[g];

		ctx->grp_infos[g] = kunit_kzalloc(test,
						  sizeof(*ctx->grp_infos[g]),
						  GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, ctx->grp_infos[g]);
		init_rwsem(&ctx->grp_infos[g]->alloc_sem);
		ctx->grp_infos[g]->bb_state = 0;
		ctx->grp_info_ptrs[g] = ctx->grp_infos[g];

		ext4_block_bitmap_set(sb, &ctx->gdps[g],
				      orphan_test_bbitmap_blk(g));
		ext4_inode_bitmap_set(sb, &ctx->gdps[g],
				      orphan_test_ibitmap_blk(g));
		ext4_inode_table_set(sb, &ctx->gdps[g],
				     orphan_test_itable_blk(g));

		ibm->bh.b_data = ibm->buf;
		ibm->bh.b_size = ORPHAN_TEST_BLK_SIZE;
		ibm->bh.b_bdev = &ctx->bdev;
		ibm->bh.b_blocknr = orphan_test_ibitmap_blk(g);
		atomic_set(&ibm->bh.b_count, 1);
		if (g == 0) {
			for (bit = 0; bit < ORPHAN_TEST_FIRST_INO - 1; bit++)
				ext4_set_bit(bit, ibm->buf);
		}
		ext4_mark_bitmap_end(ORPHAN_TEST_INODES_PER_GRP,
				     ORPHAN_TEST_BLK_SIZE * 8, ibm->buf);
		set_buffer_uptodate(&ibm->bh);
		set_bitmap_uptodate(&ibm->bh);
		set_buffer_verified(&ibm->bh);
	}
	ctx->grp_info_slice[0] = ctx->grp_info_ptrs;
	rcu_assign_pointer(sbi->s_group_info, ctx->grp_info_slice);

	root_inode = new_inode(sb);
	if (!root_inode) {
		deactivate_locked_super(sb);
		rcu_barrier();
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
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
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to create s_root");
	}

	up_write(&sb->s_umount);

	kunit_activate_static_stub(test, __ext4_journal_get_write_access,
				   orphan_test_get_write_access_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   orphan_test_dirty_metadata_stub);
	kunit_activate_static_stub(test, ext4_get_inode_loc,
				   orphan_test_get_inode_loc_stub);
	kunit_activate_static_stub(test, ext4_mark_iloc_dirty,
				   orphan_test_mark_iloc_dirty_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   orphan_test_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, __ext4_iget,
				   orphan_test_iget_stub);
	kunit_activate_static_stub(test, ext4_getblk,
				   orphan_test_getblk_stub);
	kunit_activate_static_stub(test, ext4_get_group_desc,
				   orphan_test_get_group_desc_stub);
	kunit_activate_static_stub(test, ext4_sb_getblk,
				   orphan_test_sb_getblk_stub);
	kunit_activate_static_stub(test, ext4_free_inode,
				   orphan_test_free_inode_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   orphan_test_free_blocks_stub);

	err = kunit_add_action_or_reset(test, orphan_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->enable_real_evict = true;
	return 0;
}

/* =========================================================================
 * 1. Geometry & Empty-State Helpers
 * =========================================================================
 */

struct orphan_blksize_param {
	const char *desc;
	unsigned long blocksize;
	unsigned char blocksize_bits;
	int expected_inodes_per_ob;
};

static const struct orphan_blksize_param orphan_blksize_params[] = {
	{ "1K_blocksize",  1024,  10, 254   },
	{ "2K_blocksize",  2048,  11, 510   },
	{ "4K_blocksize",  4096,  12, 1022  },
	{ "64K_blocksize", 65536, 16, 16382 },
};

KUNIT_ARRAY_PARAM_DESC(orphan_blksize, orphan_blksize_params, desc);

static void test_inodes_per_orphan_block_param(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	const struct orphan_blksize_param *p = test->param_value;

	ctx->sb->s_blocksize = p->blocksize;
	ctx->sb->s_blocksize_bits = p->blocksize_bits;

	KUNIT_EXPECT_EQ(test, ext4_inodes_per_orphan_block(ctx->sb),
			p->expected_inodes_per_ob);
}

static void test_orphan_file_empty(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;

	/* 1. Feature disabled returns 1 regardless of s_orphan_info */
	ext4_clear_feature_orphan_file(sb);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);

	/* 2. Feature enabled with 0 blocks returns 1 */
	ext4_set_feature_orphan_file(sb);
	ctx->sbi.s_orphan_info.of_blocks = 0;
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);

	/* 3. Multi-block orphan file with all slots free returns 1 */
	orphan_test_setup_static_orphan_file(ctx, 3);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);

	/* 4. Occupied entry in a non-first block makes file non-empty (0) */
	atomic_set(&ctx->static_binfo[2].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB - 1);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 0);

	/* 5. Restoring free count returns 1 again */
	atomic_set(&ctx->static_binfo[2].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);
}

/* =========================================================================
 * 2. Superblock Linked-List ext4_orphan_add()
 * =========================================================================
 */

static void test_orphan_add_early_exits_and_idempotency(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2, *s3, *s4;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 16, S_IFREG | 0644, 0);
	s3 = orphan_test_create_inode(test, ctx, 17, S_IFREG | 0644, 0);
	s4 = orphan_test_create_inode(test, ctx, 18, S_IFREG | 0644, 0);

	/* 1. !sbi->s_journal -> no-op returning 0 */
	ctx->sbi.s_journal = NULL;
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);
	ctx->sbi.s_journal = &ctx->mock_journal;

	/* 2. is_bad_inode(inode) -> no-op returning 0 */
	make_bad_inode(s2->inode);
	err = orphan_test_call_add(&ctx->mock_handle, s2->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s2->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);

	/* 3. Idempotent when already on sbi->s_orphan list */
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);

	orphan_test_reset_faults(ctx);
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);
	KUNIT_EXPECT_EQ(test, ctx->dirty_metadata_calls, 0U);

	/* 4. Idempotent when already tracked via EXT4_STATE_ORPHAN_FILE */
	ext4_set_inode_state(s3->inode, EXT4_STATE_ORPHAN_FILE);
	EXT4_I(s3->inode)->i_orphan_idx = 0;
	err = orphan_test_call_add(&ctx->mock_handle, s3->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);

	/* 5. I_NEW inode can be added & deleted without holding i_rwsem */
	spin_lock(&s4->inode->i_lock);
	inode_state_set(s4->inode, I_NEW);
	spin_unlock(&s4->inode->i_lock);

	err = ext4_orphan_add(&ctx->mock_handle, s4->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 18U);

	err = ext4_orphan_del(&ctx->mock_handle, s4->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);

	spin_lock(&s4->inode->i_lock);
	inode_state_clear(s4->inode, I_NEW);
	spin_unlock(&s4->inode->i_lock);
}

static void test_orphan_add_list_chaining_and_csum(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2, *s3;
	__le32 csum_before;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 20, S_IFDIR | 0755, 0);
	s3 = orphan_test_create_inode(test, ctx, 25, S_IFLNK | 0777, 0);

	/* Corrupt s_checksum first so we verify ext4_superblock_csum_set() */
	ctx->es.s_checksum = 0;
	csum_before = ctx->es.s_checksum;

	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s1->inode), 0U);
	KUNIT_EXPECT_NE(test, le32_to_cpu(ctx->es.s_checksum),
			le32_to_cpu(csum_before));
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	err = orphan_test_call_add(&ctx->mock_handle, s2->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 20U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s2->inode), 15U);

	err = orphan_test_call_add(&ctx->mock_handle, s3->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 25U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s3->inode), 20U);

	/* In-memory list head order must match LIFO insertion: s3 -> s2 -> s1 */
	KUNIT_EXPECT_PTR_EQ(test, ctx->sbi.s_orphan.next,
			    &EXT4_I(s3->inode)->i_orphan);
	KUNIT_EXPECT_PTR_EQ(test, EXT4_I(s3->inode)->i_orphan.next,
			    &EXT4_I(s2->inode)->i_orphan);
	KUNIT_EXPECT_PTR_EQ(test, EXT4_I(s2->inode)->i_orphan.next,
			    &EXT4_I(s1->inode)->i_orphan);
}

static void test_orphan_add_next_orphan_bounds(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s_valid, *s_oob;
	int err;

	s_valid = orphan_test_create_inode(test, ctx, 18, S_IFREG | 0644, 0);
	s_oob = orphan_test_create_inode(test, ctx, 19, S_IFREG | 0644, 0);

	/*
	 * 1. NEXT_ORPHAN == s_inodes_count (valid upper boundary):
	 *    inode is treated as already on-disk; skip on-disk modification,
	 *    link into in-memory s_orphan, and brelse(iloc.bh).
	 */
	NEXT_ORPHAN(s_valid->inode) = ORPHAN_TEST_TOTAL_INODES;
	ctx->es.s_last_orphan = cpu_to_le32(15);
	orphan_test_reset_faults(ctx);

	err = orphan_test_call_add(&ctx->mock_handle, s_valid->inode);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&EXT4_I(s_valid->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s_valid->inode),
			ORPHAN_TEST_TOTAL_INODES);
	KUNIT_EXPECT_EQ(test, ctx->dirty_metadata_calls, 0U);
	KUNIT_EXPECT_EQ(test, ctx->mark_iloc_calls, 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&s_valid->iloc_bh.b_count), 1);

	/*
	 * 2. NEXT_ORPHAN == s_inodes_count + 1 (invalid / garbage):
	 *    must overwrite NEXT_ORPHAN with old s_last_orphan (15), update
	 *    s_last_orphan to 19, and dirty both sb and iloc.
	 */
	NEXT_ORPHAN(s_oob->inode) = ORPHAN_TEST_TOTAL_INODES + 1;
	orphan_test_reset_faults(ctx);

	err = orphan_test_call_add(&ctx->mock_handle, s_oob->inode);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&EXT4_I(s_oob->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 19U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s_oob->inode), 15U);
	KUNIT_EXPECT_EQ(test, ctx->dirty_metadata_calls, 1U);
	KUNIT_EXPECT_EQ(test, ctx->mark_iloc_calls, 1U);
	KUNIT_EXPECT_EQ(test, atomic_read(&s_oob->iloc_bh.b_count), 1);
}

static void test_orphan_add_list_error_paths(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 21, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 22, S_IFREG | 0644, 0);

	/* 1. ext4_journal_get_write_access(s_sbh) fails */
	orphan_test_reset_faults(ctx);
	ctx->fail_write_access_err = -EIO;
	ctx->fail_write_access_on_call = 1;
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_NE(test, ctx->sbi.s_mount_state & EXT4_ERROR_FS, 0);

	/* 2. ext4_reserve_inode_write (ext4_get_inode_loc) fails */
	orphan_test_reset_faults(ctx);
	ctx->fail_get_iloc_err = -EIO;
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	/*
	 * 3. ext4_handle_dirty_metadata(s_sbh) fails while ext4_mark_iloc_dirty
	 *    succeeds: must still consume iloc.bh and unlink from s_orphan!
	 */
	orphan_test_reset_faults(ctx);
	ctx->fail_dirty_metadata_err = -EIO;
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, ctx->mark_iloc_calls, 1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	/*
	 * 4. ext4_handle_dirty_metadata(s_sbh) succeeds while
	 *    ext4_mark_iloc_dirty fails: err = rc and rollback from s_orphan.
	 */
	orphan_test_reset_faults(ctx);
	ctx->es.s_last_orphan = 0;
	ctx->fail_mark_iloc_err = -ENOSPC;
	err = orphan_test_call_add(&ctx->mock_handle, s2->inode);
	KUNIT_EXPECT_EQ(test, err, -ENOSPC);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s2->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, atomic_read(&s2->iloc_bh.b_count), 1);
}

/* =========================================================================
 * 3. Superblock Linked-List ext4_orphan_del()
 * =========================================================================
 */

static void test_orphan_del_early_exits(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s_untracked;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s_untracked = orphan_test_create_inode(test, ctx, 16, S_IFREG | 0644, 0);

	/* 1. Untracked inode is a fast no-op */
	orphan_test_reset_faults(ctx);
	err = orphan_test_call_del(&ctx->mock_handle, s_untracked->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx->get_iloc_calls, 0U);

	/* Add s1 to orphan list */
	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* 2. !s_journal && !(s_mount_state & EXT4_ORPHAN_FS) -> early return 0 */
	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state &= ~EXT4_ORPHAN_FS;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);

	/* 3. !s_journal WITH EXT4_ORPHAN_FS proceeds with full deletion */
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
}

static void test_orphan_del_list_positions_and_null_handle(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2, *s3;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 20, S_IFREG | 0644, 0);
	s3 = orphan_test_create_inode(test, ctx, 25, S_IFREG | 0644, 0);

	/* Build chain: s_last_orphan -> 25 (s3) -> 20 (s2) -> 15 (s1) -> 0 */
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s2->inode),
			0);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s3->inode),
			0);

	/* 1. Delete middle entry (s2 = 20): s3->NEXT_ORPHAN becomes 15 */
	err = orphan_test_call_del(&ctx->mock_handle, s2->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s2->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s2->inode), 0U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s3->inode), 15U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 25U);

	/* 2. Delete tail entry (s1 = 15): s3->NEXT_ORPHAN becomes 0 */
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s1->inode), 0U);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s3->inode), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 25U);

	/* 3. Delete head entry (s3 = 25): s_last_orphan becomes 0 */
	err = orphan_test_call_del(&ctx->mock_handle, s3->inode);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->sbi.s_orphan));
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s3->inode), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);

	/* 4. handle == NULL removes in-core entry without on-disk updates */
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	orphan_test_reset_faults(ctx);

	err = orphan_test_call_del(NULL, s1->inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, ctx->get_iloc_calls, 0U);
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);
}

static void test_orphan_del_list_error_paths(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2;
	int err;

	s1 = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 20, S_IFREG | 0644, 0);

	/* 1. Initial ext4_reserve_inode_write fails -> still unlinks in-core */
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	orphan_test_reset_faults(ctx);
	ctx->fail_get_iloc_err = -EIO;
	ctx->fail_get_iloc_on_call = 1;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	/* 2. Head delete: ext4_journal_get_write_access(s_sbh) fails */
	ctx->es.s_last_orphan = 0;
	NEXT_ORPHAN(s1->inode) = 0;
	orphan_test_reset_faults(ctx);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	orphan_test_reset_faults(ctx);
	/* Call 1 is reserve_inode_write(s1); call 2 is get_write_access(s_sbh) */
	ctx->fail_write_access_err = -EIO;
	ctx->fail_write_access_on_call = 2;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	/* 3. Head delete: ext4_handle_dirty_metadata(s_sbh) fails */
	ctx->es.s_last_orphan = 0;
	NEXT_ORPHAN(s1->inode) = 0;
	orphan_test_reset_faults(ctx);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	orphan_test_reset_faults(ctx);
	ctx->fail_dirty_metadata_err = -EIO;
	ctx->fail_dirty_metadata_on_call = 1;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);

	/* 4. Non-head delete: ext4_reserve_inode_write(i_prev) fails */
	ctx->es.s_last_orphan = 0;
	NEXT_ORPHAN(s1->inode) = 0;
	NEXT_ORPHAN(s2->inode) = 0;
	orphan_test_reset_faults(ctx);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s1->inode),
			0);
	KUNIT_ASSERT_EQ(test, orphan_test_call_add(&ctx->mock_handle, s2->inode),
			0);

	orphan_test_reset_faults(ctx);
	/* Call 1 is s1 iloc; call 2 is i_prev (s2) iloc */
	ctx->fail_get_iloc_err = -EIO;
	ctx->fail_get_iloc_on_call = 2;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s2->inode), 15U);
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&s2->iloc_bh.b_count), 1);

	/* 5. Non-head delete: ext4_mark_iloc_dirty(i_prev) fails */
	list_add(&EXT4_I(s1->inode)->i_orphan, &EXT4_I(s2->inode)->i_orphan);
	orphan_test_reset_faults(ctx);
	ctx->fail_mark_iloc_err = -EIO;
	ctx->fail_mark_iloc_on_call = 1;
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&s1->iloc_bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&s2->iloc_bh.b_count), 1);

	/* 6. Final ext4_mark_iloc_dirty(inode) fails on head delete */
	orphan_test_reset_faults(ctx);
	ctx->fail_mark_iloc_err = -ENOSPC;
	ctx->fail_mark_iloc_on_call = 1;
	err = orphan_test_call_del(&ctx->mock_handle, s2->inode);
	KUNIT_EXPECT_EQ(test, err, -ENOSPC);
	KUNIT_EXPECT_EQ(test, NEXT_ORPHAN(s2->inode), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&s2->iloc_bh.b_count), 1);
}

/* =========================================================================
 * 4. Orphan-File ext4_orphan_add() & ext4_orphan_del()
 * =========================================================================
 */

static void test_orphan_file_add_and_del_basic(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct orphan_test_inode_slot *s1;
	unsigned int blk, off;
	__le32 *bdata;
	int err;

	orphan_test_setup_static_orphan_file(ctx, 2);
	s1 = orphan_test_create_inode(test, ctx, 24, S_IFREG | 0644, 0);

	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_TRUE(test, ext4_inode_orphan_tracked(s1->inode));
	KUNIT_EXPECT_EQ(test, ctx->last_trigger_type, EXT4_JTR_ORPHAN_FILE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 0);

	blk = EXT4_I(s1->inode)->i_orphan_idx / ORPHAN_TEST_INODES_PER_OB;
	off = EXT4_I(s1->inode)->i_orphan_idx % ORPHAN_TEST_INODES_PER_OB;
	KUNIT_ASSERT_LT(test, blk, 2U);
	KUNIT_EXPECT_EQ(test, off, 0U);
	bdata = (__le32 *)ctx->static_binfo[blk].ob_bh->b_data;
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata[off]), 24U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[blk].ob_free_entries),
			(int)(ORPHAN_TEST_INODES_PER_OB - 1));

	/* Delete from orphan file and verify slot & state restoration */
	orphan_test_reset_faults(ctx);
	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_FALSE(test, ext4_inode_orphan_tracked(s1->inode));
	KUNIT_EXPECT_EQ(test, ctx->last_trigger_type, EXT4_JTR_ORPHAN_FILE);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata[off]), 0U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[blk].ob_free_entries),
			(int)ORPHAN_TEST_INODES_PER_OB);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);
}

static void test_orphan_file_add_block_wraparound_and_last_slot(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1, *s2;
	__le32 *bdata;
	unsigned int j;
	int b, err;

	orphan_test_setup_static_orphan_file(ctx, 3);
	s1 = orphan_test_create_inode(test, ctx, 30, S_IFREG | 0644, 0);
	s2 = orphan_test_create_inode(test, ctx, 31, S_IFREG | 0644, 0);

	/*
	 * Mark blocks 0 and 1 full (0 free), and leave ONLY the very last slot
	 * (ORPHAN_TEST_INODES_PER_OB - 1 = 253) free in block 2. Then test a
	 * second add when only block 0 has 1 slot free, guaranteeing
	 * wrap-around across the circular block ring regardless of CPU id.
	 */
	atomic_set(&ctx->static_binfo[0].ob_free_entries, 0);
	atomic_set(&ctx->static_binfo[1].ob_free_entries, 0);
	atomic_set(&ctx->static_binfo[2].ob_free_entries, 1);
	bdata = (__le32 *)ctx->static_binfo[2].ob_bh->b_data;
	for (j = 0; j < ORPHAN_TEST_INODES_PER_OB - 1; j++)
		bdata[j] = cpu_to_le32(100 + j);
	bdata[ORPHAN_TEST_INODES_PER_OB - 1] = 0;

	err = orphan_test_call_add(&ctx->mock_handle, s1->inode);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(s1->inode)->i_orphan_idx,
			2 * ORPHAN_TEST_INODES_PER_OB +
			(ORPHAN_TEST_INODES_PER_OB - 1));
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(bdata[ORPHAN_TEST_INODES_PER_OB - 1]), 30U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[2].ob_free_entries), 0);

	/* Now make only block 0 slot (ORPHAN_TEST_INODES_PER_OB - 1) free */
	for (b = 0; b < 3; b++)
		atomic_set(&ctx->static_binfo[b].ob_free_entries, 0);
	atomic_set(&ctx->static_binfo[0].ob_free_entries, 1);
	bdata = (__le32 *)ctx->static_binfo[0].ob_bh->b_data;
	for (j = 0; j < ORPHAN_TEST_INODES_PER_OB - 1; j++)
		bdata[j] = cpu_to_le32(100 + j);
	bdata[ORPHAN_TEST_INODES_PER_OB - 1] = 0;

	err = orphan_test_call_add(&ctx->mock_handle, s2->inode);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, EXT4_I(s2->inode)->i_orphan_idx,
			ORPHAN_TEST_INODES_PER_OB - 1);
	KUNIT_EXPECT_EQ(test,
			le32_to_cpu(bdata[ORPHAN_TEST_INODES_PER_OB - 1]), 31U);
}

static void test_orphan_file_add_enospc_fallback_and_write_err(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s_fb, *s_err, *s_dirty_err;
	int err;

	orphan_test_setup_static_orphan_file(ctx, 2);
	s_fb = orphan_test_create_inode(test, ctx, 32, S_IFREG | 0644, 0);
	s_err = orphan_test_create_inode(test, ctx, 33, S_IFREG | 0644, 0);
	s_dirty_err = orphan_test_create_inode(test, ctx, 34, S_IFREG | 0644, 0);

	/*
	 * 1. Full orphan file (all ob_free_entries == 0):
	 *    ext4_orphan_file_add() returns -ENOSPC, and ext4_orphan_add()
	 *    falls back to the superblock linked list!
	 */
	atomic_set(&ctx->static_binfo[0].ob_free_entries, 0);
	atomic_set(&ctx->static_binfo[1].ob_free_entries, 0);

	err = orphan_test_call_add(&ctx->mock_handle, s_fb->inode);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s_fb->inode,
						 EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_FALSE(test, list_empty(&EXT4_I(s_fb->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 32U);

	/*
	 * 2. Non-ENOSPC error (-EIO from ext4_journal_get_write_access):
	 *    must restore ob_free_entries via atomic_inc and return -EIO
	 *    directly WITHOUT falling back to the superblock linked list!
	 */
	atomic_set(&ctx->static_binfo[0].ob_free_entries, 5);
	atomic_set(&ctx->static_binfo[1].ob_free_entries, 0);
	orphan_test_reset_faults(ctx);
	ctx->fail_write_access_err = -EIO;
	ctx->fail_write_access_on_call = 1;

	err = orphan_test_call_add(&ctx->mock_handle, s_err->inode);

	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[0].ob_free_entries), 5);
	KUNIT_EXPECT_FALSE(test, ext4_inode_orphan_tracked(s_err->inode));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 32U);

	/*
	 * 3. Non-ENOSPC error (-EIO from ext4_handle_dirty_metadata):
	 *    propagates -EIO directly without falling back to s_last_orphan.
	 */
	orphan_test_reset_faults(ctx);
	ctx->fail_dirty_metadata_err = -EIO;
	ctx->fail_dirty_metadata_on_call = 1;

	err = orphan_test_call_add(&ctx->mock_handle, s_dirty_err->inode);

	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(s_dirty_err->inode,
						EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_PTR_EQ(test, ctx->sbi.s_orphan.next,
			    &EXT4_I(s_fb->inode)->i_orphan);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 32U);
}

static void test_orphan_file_del_edge_and_error_paths(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct orphan_test_inode_slot *s1;
	__le32 *bdata;
	int err;

	orphan_test_setup_static_orphan_file(ctx, 2);
	s1 = orphan_test_create_inode(test, ctx, 35, S_IFREG | 0644, 0);
	bdata = (__le32 *)ctx->static_binfo[1].ob_bh->b_data;

	/* 1. handle == NULL clears in-core state without touching orphan block */
	bdata[4] = cpu_to_le32(35);
	atomic_set(&ctx->static_binfo[1].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB - 1);
	EXT4_I(s1->inode)->i_orphan_idx = 1 * ORPHAN_TEST_INODES_PER_OB + 4;
	ext4_set_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE);

	err = orphan_test_call_del(NULL, s1->inode);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_TRUE(test, list_empty(&EXT4_I(s1->inode)->i_orphan));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata[4]), 35U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[1].ob_free_entries),
			(int)(ORPHAN_TEST_INODES_PER_OB - 1));

	/* 2. Out-of-range blk >= oi->of_blocks hits WARN_ON_ONCE guard safely */
	EXT4_I(s1->inode)->i_orphan_idx = 2 * ORPHAN_TEST_INODES_PER_OB;
	ext4_set_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE);
	orphan_test_reset_faults(ctx);

	kunit_warning_suppress(test) {
		err = orphan_test_call_del(&ctx->mock_handle, s1->inode);
	}

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_EQ(test, ctx->write_access_calls, 0U);

	/* 3. ext4_journal_get_write_access error leaves bdata[off] intact */
	EXT4_I(s1->inode)->i_orphan_idx = 1 * ORPHAN_TEST_INODES_PER_OB + 4;
	ext4_set_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE);
	orphan_test_reset_faults(ctx);
	ctx->fail_write_access_err = -EIO;

	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);

	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata[4]), 35U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[1].ob_free_entries),
			(int)(ORPHAN_TEST_INODES_PER_OB - 1));

	/* 4. ext4_handle_dirty_metadata error propagates after clearing slot */
	EXT4_I(s1->inode)->i_orphan_idx = 1 * ORPHAN_TEST_INODES_PER_OB + 4;
	ext4_set_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE);
	orphan_test_reset_faults(ctx);
	ctx->fail_dirty_metadata_err = -ENOSPC;

	err = orphan_test_call_del(&ctx->mock_handle, s1->inode);

	KUNIT_EXPECT_EQ(test, err, -ENOSPC);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(s1->inode, EXT4_STATE_ORPHAN_FILE));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata[4]), 0U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[1].ob_free_entries),
			(int)ORPHAN_TEST_INODES_PER_OB);
}

/* =========================================================================
 * 5. Trigger, Init & Release of Orphan File Info
 * =========================================================================
 */

static void test_orphan_file_block_trigger_and_frozen_data(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct jbd2_buffer_trigger_type *triggers =
		&ctx->sbi.s_journal_triggers[EXT4_JTR_ORPHAN_FILE].tr_triggers;
	struct buffer_head *bh;
	struct ext4_orphan_block_tail *ot;
	__le32 *bdata, *fdata;
	__u32 expected_csum;

	orphan_test_setup_static_orphan_file(ctx, 1);
	bh = ctx->static_binfo[0].ob_bh;
	ot = (struct ext4_orphan_block_tail *)(bh->b_data + sb->s_blocksize -
					       sizeof(*ot));
	bdata = (__le32 *)bh->b_data;
	bdata[0] = cpu_to_le32(42);
	ot->ob_checksum = 0;

	/* 1. Trigger with data == bh->b_data computes valid block checksum */
	ext4_orphan_file_block_trigger(triggers, bh, bh->b_data, bh->b_size);
	expected_csum = orphan_test_calc_ob_csum(sb, ORPHAN_TEST_OF_CSUM_SEED,
						 bh->b_blocknr, bh->b_data);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ot->ob_checksum), expected_csum);

	/* 2. Trigger with separate JBD2 frozen buffer hashes @data, not bh */
	memset(ctx->frozen_buf, 0, sizeof(ctx->frozen_buf));
	fdata = (__le32 *)ctx->frozen_buf;
	fdata[0] = cpu_to_le32(99);
	fdata[10] = cpu_to_le32(105);

	ext4_orphan_file_block_trigger(triggers, bh, ctx->frozen_buf,
				       bh->b_size);
	expected_csum = orphan_test_calc_ob_csum(sb, ORPHAN_TEST_OF_CSUM_SEED,
						 bh->b_blocknr,
						 ctx->frozen_buf);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ot->ob_checksum), expected_csum);
}

static void test_init_and_release_orphan_info_lifecycle(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_orphan_info *oi = &ctx->sbi.s_orphan_info;
	struct orphan_test_inode_slot *of_slot;
	struct ext4_orphan_block_tail *ot;
	__le32 *bdata;
	int i, err;

	/* 1. Feature disabled: init and release are clean no-ops */
	ext4_clear_feature_orphan_file(sb);
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, oi->of_blocks, 0);
	KUNIT_EXPECT_NULL(test, oi->of_binfo);
	ext4_release_orphan_info(sb);

	/* 2. Valid 2-block orphan file load and release */
	ext4_set_feature_orphan_file(sb);
	of_slot = orphan_test_create_inode(test, ctx, ORPHAN_TEST_ORPHAN_INO,
					   S_IFREG | 0600, 1);
	of_slot->inode->i_size = 2 * ORPHAN_TEST_BLK_SIZE;
	EXT4_I(of_slot->inode)->i_csum_seed = ORPHAN_TEST_OF_CSUM_SEED;

	for (i = 0; i < 2; i++) {
		struct orphan_test_ob_blk *oblk = &ctx->ob_pool[i];

		memset(oblk, 0, sizeof(*oblk));
		oblk->present = true;
		oblk->bh.b_data = oblk->buf;
		oblk->bh.b_size = sb->s_blocksize;
		oblk->bh.b_bdev = &ctx->bdev;
		oblk->bh.b_blocknr = ORPHAN_TEST_OB_BASE_BLK + i;
		atomic_set(&oblk->bh.b_count, 1);
		set_buffer_uptodate(&oblk->bh);
	}
	/* Populate 3 active entries in block 0, 0 entries in block 1 */
	bdata = (__le32 *)ctx->ob_pool[0].buf;
	bdata[0] = cpu_to_le32(20);
	bdata[5] = cpu_to_le32(21);
	bdata[253] = cpu_to_le32(22);
	orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED,
			       &ctx->ob_pool[0].bh, EXT4_ORPHAN_BLOCK_MAGIC);
	orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED,
			       &ctx->ob_pool[1].bh, EXT4_ORPHAN_BLOCK_MAGIC);

	err = ext4_init_orphan_info(sb);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->orphan_info_allocated = true;

	KUNIT_EXPECT_EQ(test, oi->of_blocks, 2);
	KUNIT_EXPECT_EQ(test, oi->of_csum_seed, ORPHAN_TEST_OF_CSUM_SEED);
	KUNIT_ASSERT_NOT_NULL(test, oi->of_binfo);
	KUNIT_EXPECT_EQ(test, atomic_read(&oi->of_binfo[0].ob_free_entries),
			(int)(ORPHAN_TEST_INODES_PER_OB - 3));
	KUNIT_EXPECT_EQ(test, atomic_read(&oi->of_binfo[1].ob_free_entries),
			(int)ORPHAN_TEST_INODES_PER_OB);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&of_slot->inode->i_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[1].bh.b_count), 2);

	/* Release drops every block bh reference and frees of_binfo */
	ext4_release_orphan_info(sb);
	ctx->orphan_info_allocated = false;
	memset(oi, 0, sizeof(*oi));
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[1].bh.b_count), 1);

	/*
	 * 3. With metadata_csum disabled, ext4_orphan_file_block_csum_verify()
	 *    returns 1 even if ob_checksum does not match.
	 */
	ext4_clear_feature_metadata_csum(sb);
	of_slot->inode->i_size = ORPHAN_TEST_BLK_SIZE;
	ot = (struct ext4_orphan_block_tail *)(ctx->ob_pool[0].buf +
					       sb->s_blocksize - sizeof(*ot));
	ot->ob_checksum = cpu_to_le32(0xdeadbeefU);

	err = ext4_init_orphan_info(sb);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->orphan_info_allocated = true;

	ext4_release_orphan_info(sb);
	ctx->orphan_info_allocated = false;
	memset(oi, 0, sizeof(*oi));
}

static void test_init_orphan_info_max_blocks_boundary(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_orphan_info *oi = &ctx->sbi.s_orphan_info;
	struct orphan_test_inode_slot *of_slot;
	int err;

	ext4_set_feature_orphan_file(sb);
	of_slot = orphan_test_create_inode(test, ctx, ORPHAN_TEST_ORPHAN_INO,
					   S_IFREG | 0600, 1);
	EXT4_I(of_slot->inode)->i_csum_seed = ORPHAN_TEST_OF_CSUM_SEED;

	/*
	 * 1. Size 1 byte over max(EXT4_MAX_ORPHAN_FILE_BLOCKS << blkbits, 2M)
	 *    (2 MiB = 2048 1 KiB blocks) fails with -EFSCORRUPTED.
	 */
	of_slot->inode->i_size = (2LL << 20) + 1;
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, atomic_read(&of_slot->inode->i_count), 1);

	/* 2. Exact 2 MiB (2048-block) limit succeeds and releases all bh refs */
	memset(ctx->boundary_buf, 0, sizeof(ctx->boundary_buf));
	ctx->boundary_bh.b_data = ctx->boundary_buf;
	ctx->boundary_bh.b_size = sb->s_blocksize;
	ctx->boundary_bh.b_bdev = &ctx->bdev;
	ctx->boundary_bh.b_blocknr = ORPHAN_TEST_OB_BASE_BLK;
	atomic_set(&ctx->boundary_bh.b_count, 1);
	set_buffer_uptodate(&ctx->boundary_bh);
	orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED,
			       &ctx->boundary_bh, EXT4_ORPHAN_BLOCK_MAGIC);

	ctx->use_boundary_bh = true;
	of_slot->inode->i_size = 2LL << 20;

	err = ext4_init_orphan_info(sb);
	KUNIT_ASSERT_EQ(test, err, 0);
	ctx->orphan_info_allocated = true;
	KUNIT_EXPECT_EQ(test, oi->of_blocks, 2048);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->boundary_bh.b_count), 2049);

	ext4_release_orphan_info(sb);
	ctx->orphan_info_allocated = false;
	memset(oi, 0, sizeof(*oi));
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->boundary_bh.b_count), 1);
}

static void test_init_orphan_info_error_paths_and_cleanup(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct ext4_orphan_info *oi = &ctx->sbi.s_orphan_info;
	struct orphan_test_inode_slot *of_slot;
	struct ext4_orphan_block_tail *ot;
	int i, err;

	ext4_set_feature_orphan_file(sb);

	/* 1. ext4_iget failure returns error immediately */
	ctx->fail_iget_err = -ENOENT;
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -ENOENT);
	orphan_test_reset_faults(ctx);

	/* Prepare 2-block orphan inode where block 0 loads cleanly */
	of_slot = orphan_test_create_inode(test, ctx, ORPHAN_TEST_ORPHAN_INO,
					   S_IFREG | 0600, 1);
	of_slot->inode->i_size = 2 * ORPHAN_TEST_BLK_SIZE;
	EXT4_I(of_slot->inode)->i_csum_seed = ORPHAN_TEST_OF_CSUM_SEED;

	for (i = 0; i < 2; i++) {
		struct orphan_test_ob_blk *oblk = &ctx->ob_pool[i];

		memset(oblk, 0, sizeof(*oblk));
		oblk->present = true;
		oblk->bh.b_data = oblk->buf;
		oblk->bh.b_size = sb->s_blocksize;
		oblk->bh.b_bdev = &ctx->bdev;
		oblk->bh.b_blocknr = ORPHAN_TEST_OB_BASE_BLK + i;
		atomic_set(&oblk->bh.b_count, 1);
		set_buffer_uptodate(&oblk->bh);
		orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED, &oblk->bh,
				       EXT4_ORPHAN_BLOCK_MAGIC);
	}

	/* 2. Block 1 ext4_bread returns ERR_PTR(-EIO) -> block 0 brelse'd */
	ctx->ob_pool[1].bread_err = -EIO;
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&of_slot->inode->i_count), 1);
	memset(oi, 0, sizeof(*oi));
	ctx->ob_pool[1].bread_err = 0;

	/* 3. Block 1 ext4_bread returns NULL (unmapped hole) -> -EIO */
	ctx->ob_pool[1].return_hole = true;
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 1);
	memset(oi, 0, sizeof(*oi));
	ctx->ob_pool[1].return_hole = false;

	/* 4. Block 1 has invalid ob_magic -> ext4_error + -EIO + brelse */
	orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED,
			       &ctx->ob_pool[1].bh, 0xbad0bad0U);
	orphan_test_reset_faults(ctx);
	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_NE(test, ctx->sbi.s_mount_state & EXT4_ERROR_FS, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[1].bh.b_count), 1);
	memset(oi, 0, sizeof(*oi));

	/* 5. Block 1 has valid magic but corrupted ob_checksum -> -EIO */
	orphan_test_seal_ob_bh(sb, ORPHAN_TEST_OF_CSUM_SEED,
			       &ctx->ob_pool[1].bh, EXT4_ORPHAN_BLOCK_MAGIC);
	ot = (struct ext4_orphan_block_tail *)(ctx->ob_pool[1].buf +
					       sb->s_blocksize - sizeof(*ot));
	ot->ob_checksum = cpu_to_le32(le32_to_cpu(ot->ob_checksum) ^ 0x1U);
	orphan_test_reset_faults(ctx);

	err = ext4_init_orphan_info(sb);
	KUNIT_EXPECT_EQ(test, err, -EIO);
	KUNIT_EXPECT_NE(test, ctx->sbi.s_mount_state & EXT4_ERROR_FS, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[0].bh.b_count), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->ob_pool[1].bh.b_count), 1);
	memset(oi, 0, sizeof(*oi));
}

/* =========================================================================
 * 6. End-to-End Orphan Recovery: ext4_orphan_cleanup() & ext4_process_orphan()
 * =========================================================================
 */

static void test_orphan_cleanup_skip_guards(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	__le32 saved_ro_compat;

	/* 1. Empty s_last_orphan and empty orphan file -> fast return */
	ctx->es.s_last_orphan = 0;
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);

	/* 2. Read-only block device skips orphan cleanup */
	ctx->es.s_last_orphan = cpu_to_le32(15);
	bdev_set_flag(&ctx->bdev, BD_READ_ONLY);
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);

	/* 3. Unknown ROCOMPAT feature fails ext4_feature_set_ok(sb, 0) */
	saved_ro_compat = ctx->es.s_feature_ro_compat;
	ctx->es.s_feature_ro_compat |= cpu_to_le32(0x80000000U);
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	ctx->es.s_feature_ro_compat = saved_ro_compat;

	/* 4a. EXT4_ERROR_FS on SB_RDONLY mount preserves s_last_orphan */
	ctx->sbi.s_mount_state |= EXT4_ERROR_FS;
	sb->s_flags |= SB_RDONLY;
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 15U);
	sb->s_flags &= ~SB_RDONLY;

	/* 4b. EXT4_ERROR_FS on R/W mount clears s_last_orphan to 0 */
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;
}

static void
test_orphan_cleanup_linked_list_unlinked_and_truncated(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct orphan_test_inode_slot *s_unlinked, *s_trunc, *s_symlink;

	/*
	 * Match ext4_fill_super() no-journal recovery state:
	 * s_journal = NULL, s_mount_state |= EXT4_ORPHAN_FS.
	 */
	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;
	set_opt(sb, DEBUG);

	/*
	 * Build on-disk orphan chain:
	 * s_last_orphan -> 15 (unlinked reg w/ extent) -> 16 (linked truncate
	 * w/ extent) -> 17 (unlinked fast symlink) -> 0
	 */
	s_unlinked = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s_unlinked->handoff_to_iget = true;
	orphan_test_add_single_extent(s_unlinked->inode, 0, 200ULL, 2);
	NEXT_ORPHAN(s_unlinked->inode) = 16;

	s_trunc = orphan_test_create_inode(test, ctx, 16, S_IFREG | 0644, 1);
	s_trunc->handoff_to_iget = true;
	s_trunc->inode->i_size = 0;
	EXT4_I(s_trunc->inode)->i_disksize = 2 * ORPHAN_TEST_BLK_SIZE;
	orphan_test_add_single_extent(s_trunc->inode, 0, 300ULL, 2);
	NEXT_ORPHAN(s_trunc->inode) = 17;

	s_symlink = orphan_test_create_inode(test, ctx, 17, S_IFLNK | 0777, 0);
	s_symlink->handoff_to_iget = true;
	s_symlink->inode->i_size = 8;
	memcpy(EXT4_I(s_symlink->inode)->i_data, "target12", 8);
	NEXT_ORPHAN(s_symlink->inode) = 0;

	ctx->es.s_last_orphan = cpu_to_le32(15);

	ext4_orphan_cleanup(sb, &ctx->es);

	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->sbi.s_orphan));
	KUNIT_EXPECT_EQ(test, ctx->nr_freed_inos, 2U);
	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 15));
	KUNIT_EXPECT_FALSE(test, orphan_test_was_ino_freed(ctx, 16));
	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 17));
	/* Both s_unlinked (2 blks) and s_trunc (2 blks) freed their extents */
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blks, 4UL);
}

static void
test_orphan_cleanup_orphan_file_and_rdonly_restore(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct orphan_test_inode_slot *s_list, *s_of_del, *s_of_trunc, *s_of_only;
	__le32 *bdata0, *bdata1;

	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;
	sb->s_flags |= SB_RDONLY;

	orphan_test_setup_static_orphan_file(ctx, 2);
	bdata0 = (__le32 *)ctx->static_binfo[0].ob_bh->b_data;
	bdata1 = (__le32 *)ctx->static_binfo[1].ob_bh->b_data;

	/* 1. Linked-list unlinked orphan (ino 15) */
	s_list = orphan_test_create_inode(test, ctx, 15, S_IFREG | 0644, 0);
	s_list->handoff_to_iget = true;
	NEXT_ORPHAN(s_list->inode) = 0;
	ctx->es.s_last_orphan = cpu_to_le32(15);

	/*
	 * 2. Orphan file entries:
	 *    - Block 0 slot 2: valid unlinked orphan (ino 16)
	 *    - Block 0 slot 10: unallocated ino 99 -> ext4_orphan_get() fails,
	 *      loop must continue to remaining entries!
	 *    - Block 1 slot 5: valid linked truncate orphan (ino 18)
	 */
	s_of_del = orphan_test_create_inode(test, ctx, 16, S_IFREG | 0644, 0);
	s_of_del->handoff_to_iget = true;
	bdata0[2] = cpu_to_le32(16);
	bdata0[10] = cpu_to_le32(99);
	atomic_set(&ctx->static_binfo[0].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB - 2);

	s_of_trunc = orphan_test_create_inode(test, ctx, 18, S_IFREG | 0644, 1);
	s_of_trunc->handoff_to_iget = true;
	s_of_trunc->inode->i_size = 0;
	orphan_test_add_single_extent(s_of_trunc->inode, 0, 400ULL, 1);
	bdata1[5] = cpu_to_le32(18);
	atomic_set(&ctx->static_binfo[1].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB - 1);

	ext4_orphan_cleanup(sb, &ctx->es);

	/* SB_RDONLY was cleared during cleanup and restored upon return */
	KUNIT_EXPECT_TRUE(test, ctx->saw_rw_during_cleanup);
	KUNIT_EXPECT_NE(test, sb->s_flags & SB_RDONLY, 0UL);

	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 15));
	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 16));
	KUNIT_EXPECT_FALSE(test, orphan_test_was_ino_freed(ctx, 18));
	KUNIT_EXPECT_EQ(test, ctx->total_freed_blks, 1UL);

	/* Valid orphan-file slots were cleared; bad slot 10 was skipped */
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata0[2]), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata0[10]), 99U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata1[5]), 0U);
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[0].ob_free_entries),
			(int)(ORPHAN_TEST_INODES_PER_OB - 1));
	KUNIT_EXPECT_EQ(test,
			atomic_read(&ctx->static_binfo[1].ob_free_entries),
			(int)ORPHAN_TEST_INODES_PER_OB);

	/*
	 * 3. s_last_orphan == 0 with non-empty orphan file:
	 *    must NOT early-return at (!es->s_last_orphan && orphan_file_empty).
	 */
	bdata0[10] = 0;
	atomic_set(&ctx->static_binfo[0].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB);
	s_of_only = orphan_test_create_inode(test, ctx, 19, S_IFREG | 0644, 0);
	s_of_only->handoff_to_iget = true;
	bdata1[8] = cpu_to_le32(19);
	atomic_set(&ctx->static_binfo[1].ob_free_entries,
		   ORPHAN_TEST_INODES_PER_OB - 1);
	orphan_test_reset_faults(ctx);

	ext4_orphan_cleanup(sb, &ctx->es);

	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 19));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(bdata1[8]), 0U);
	KUNIT_EXPECT_EQ(test, ext4_orphan_file_empty(sb), 1);
}

static void
test_orphan_cleanup_list_errors_and_truncate_fail(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct orphan_test_inode_slot *s1, *s2;

	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;

	/*
	 * 1. Reserved / invalid orphan ino (< EXT4_FIRST_INO) on s_last_orphan:
	 *    ext4_orphan_get() returns -EFSCORRUPTED, loop clears s_last_orphan
	 *    and breaks cleanly.
	 */
	ctx->es.s_last_orphan = cpu_to_le32(5);
	ext4_orphan_cleanup(sb, &ctx->es);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);

	/*
	 * 2. ext4_truncate() fails (-EIO) on linked orphan s1 (20), which
	 *    points to s2 (21):
	 *    - ext4_process_orphan() removes s1 from in-core s_orphan via
	 *      ext4_orphan_del(NULL, s1) and sets EXT4_ERROR_FS via
	 *      ext4_std_error().
	 *    - Next loop iteration detects EXT4_ERROR_FS, clears s_last_orphan,
	 *      and aborts recovery before touching s2 (21).
	 */
	orphan_test_reset_faults(ctx);
	kunit_activate_static_stub(test, ext4_truncate,
				   orphan_test_truncate_stub);
	ctx->stub_truncate_err = -EIO;

	s1 = orphan_test_create_inode(test, ctx, 20, S_IFREG | 0644, 1);
	s1->handoff_to_iget = true;
	NEXT_ORPHAN(s1->inode) = 21;

	s2 = orphan_test_create_inode(test, ctx, 21, S_IFREG | 0644, 0);
	s2->handoff_to_iget = true;
	NEXT_ORPHAN(s2->inode) = 0;

	ctx->es.s_last_orphan = cpu_to_le32(20);

	ext4_orphan_cleanup(sb, &ctx->es);
	kunit_deactivate_static_stub(test, ext4_truncate);

	KUNIT_EXPECT_EQ(test, ctx->truncate_calls, 1U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->sbi.s_orphan));
	KUNIT_EXPECT_FALSE(test, orphan_test_was_ino_freed(ctx, 21));
	KUNIT_EXPECT_NOT_NULL(test, s2->inode);
}

#ifdef CONFIG_QUOTA
static void test_orphan_cleanup_quota_paths(struct kunit *test)
{
	struct orphan_test_ctx *ctx = test->priv;
	struct super_block *sb = ctx->sb;
	struct orphan_test_inode_slot *s1, *s2;

	ctx->sbi.s_journal = NULL;
	ctx->sbi.s_mount_state |= EXT4_ORPHAN_FS;
	sb->s_flags |= SB_RDONLY;
	strscpy(ctx->qf_name, "aquota.user", sizeof(ctx->qf_name));

	/*
	 * 1. Exercise journaled quota mount hook (ext4_quota_on_mount) and,
	 *    when CONFIG_QFMT_V2 is also enabled, ext4_enable_quotas() success
	 *    (s_usr_quota_inum == 0 -> quota_update = 1) on SB_RDONLY orphan
	 *    recovery. Hold s_umount to satisfy lockdep in s_qf_names RCU
	 *    dereference (matching mount-time ext4_fill_super context).
	 */
	rcu_assign_pointer(ctx->sbi.s_qf_names[USRQUOTA], ctx->qf_name);
#if IS_ENABLED(CONFIG_QFMT_V2)
	ext4_set_feature_quota(sb);
	ctx->es.s_usr_quota_inum = 0;
#endif

	s1 = orphan_test_create_inode(test, ctx, 28, S_IFREG | 0644, 0);
	s1->handoff_to_iget = true;
	NEXT_ORPHAN(s1->inode) = 0;
	ctx->es.s_last_orphan = cpu_to_le32(28);

	down_write(&sb->s_umount);
	ext4_orphan_cleanup(sb, &ctx->es);
	rcu_assign_pointer(ctx->sbi.s_qf_names[USRQUOTA], NULL);
	up_write(&sb->s_umount);

	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_TRUE(test, orphan_test_was_ino_freed(ctx, 28));
	KUNIT_EXPECT_NE(test, sb->s_flags & SB_RDONLY, 0UL);

#if IS_ENABLED(CONFIG_QFMT_V2)
	/*
	 * 2. Exercise ext4_enable_quotas() failure path: missing quota inode 3
	 *    triggers ext4_error() (setting EXT4_ERROR_FS), logs the quota
	 *    turn-on error, aborts s_last_orphan recovery while clearing
	 *    s_last_orphan to 0, and still restores SB_RDONLY.
	 */
	orphan_test_reset_faults(ctx);
	ctx->es.s_usr_quota_inum = cpu_to_le32(EXT4_USR_QUOTA_INO);
	s2 = orphan_test_create_inode(test, ctx, 29, S_IFREG | 0644, 0);
	s2->handoff_to_iget = true;
	NEXT_ORPHAN(s2->inode) = 0;
	ctx->es.s_last_orphan = cpu_to_le32(29);

	down_write(&sb->s_umount);
	ext4_orphan_cleanup(sb, &ctx->es);
	up_write(&sb->s_umount);

	KUNIT_EXPECT_NE(test, ctx->sbi.s_mount_state & EXT4_ERROR_FS, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_orphan), 0U);
	KUNIT_EXPECT_FALSE(test, orphan_test_was_ino_freed(ctx, 29));
	KUNIT_EXPECT_NE(test, sb->s_flags & SB_RDONLY, 0UL);
#else
	(void)s2;
#endif
}
#endif

static struct kunit_case orphan_test_cases[] = {
	KUNIT_CASE_PARAM(test_inodes_per_orphan_block_param,
			 orphan_blksize_gen_params),
	KUNIT_CASE(test_orphan_file_empty),
	KUNIT_CASE(test_orphan_add_early_exits_and_idempotency),
	KUNIT_CASE(test_orphan_add_list_chaining_and_csum),
	KUNIT_CASE(test_orphan_add_next_orphan_bounds),
	KUNIT_CASE(test_orphan_add_list_error_paths),
	KUNIT_CASE(test_orphan_del_early_exits),
	KUNIT_CASE(test_orphan_del_list_positions_and_null_handle),
	KUNIT_CASE(test_orphan_del_list_error_paths),
	KUNIT_CASE(test_orphan_file_add_and_del_basic),
	KUNIT_CASE(test_orphan_file_add_block_wraparound_and_last_slot),
	KUNIT_CASE(test_orphan_file_add_enospc_fallback_and_write_err),
	KUNIT_CASE(test_orphan_file_del_edge_and_error_paths),
	KUNIT_CASE(test_orphan_file_block_trigger_and_frozen_data),
	KUNIT_CASE(test_init_and_release_orphan_info_lifecycle),
	KUNIT_CASE(test_init_orphan_info_max_blocks_boundary),
	KUNIT_CASE(test_init_orphan_info_error_paths_and_cleanup),
	KUNIT_CASE(test_orphan_cleanup_skip_guards),
	KUNIT_CASE(test_orphan_cleanup_linked_list_unlinked_and_truncated),
	KUNIT_CASE(test_orphan_cleanup_orphan_file_and_rdonly_restore),
	KUNIT_CASE(test_orphan_cleanup_list_errors_and_truncate_fail),
#ifdef CONFIG_QUOTA
	KUNIT_CASE(test_orphan_cleanup_quota_paths),
#endif
	{}
};

static struct kunit_suite orphan_test_suite = {
	.name = "ext4_orphan_test",
	.init = orphan_test_init,
	.test_cases = orphan_test_cases,
};

kunit_test_suites(&orphan_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 orphan inode handling");
MODULE_LICENSE("GPL");
