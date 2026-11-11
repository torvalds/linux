// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for fs/ext4/block_validity.c
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "ext4.h"
#include "ext4_extents.h"
#include "ext4_jbd2.h"
#include "xattr.h"

/*
 * Default mock filesystem geometry:
 *  - 4 block groups, 1024-byte blocks, s_first_data_block = 1
 *  - 1024 blocks per group -> total blocks = 4097 (valid blocks 0..4096)
 *  - sparse_super enabled, s_gdb_count = 1, s_reserved_gdt_blocks = 2:
 *      group 0 (blocks    1..1024): 4 base meta blocks (1..4)
 *      group 1 (blocks 1025..2048): 4 base meta blocks (1025..1028)
 *      group 2 (blocks 2049..3072): 0 base meta blocks (sparse_super)
 *      group 3 (blocks 3073..4096): 4 base meta blocks (3073..3076)
 *  - 32 inodes per group (128 total), 256-byte inodes -> 8 inode table
 *    blocks per group
 */
#define BV_TEST_NGROUPS			4U
#define BV_TEST_BLK_SIZE		1024U
#define BV_TEST_BLK_BITS		10U
#define BV_TEST_BLOCKS_PER_GRP		1024U
#define BV_TEST_FIRST_DATA_BLK		1U
#define BV_TEST_TOTAL_BLKS		(BV_TEST_FIRST_DATA_BLK + \
					 BV_TEST_NGROUPS * \
					 BV_TEST_BLOCKS_PER_GRP)
#define BV_TEST_INODE_SIZE		256U
#define BV_TEST_EXTRA_ISIZE		32U
#define BV_TEST_INODES_PER_BLK		(BV_TEST_BLK_SIZE / BV_TEST_INODE_SIZE)
#define BV_TEST_INODES_PER_GRP		32U
#define BV_TEST_ITB_PER_GRP		(BV_TEST_INODES_PER_GRP / \
					 BV_TEST_INODES_PER_BLK)
#define BV_TEST_TOTAL_INODES		(BV_TEST_NGROUPS * \
					 BV_TEST_INODES_PER_GRP)
#define BV_TEST_FIRST_INO		11U
#define BV_TEST_GDB_COUNT		1U
#define BV_TEST_RSV_GDT_BLKS		2U
#define BV_TEST_GDESC_BLK		2ULL
#define BV_TEST_MAX_SLOTS		8U

struct bv_test_inode_slot {
	bool in_use;
	unsigned long ino;
	struct inode *inode;
};

struct bv_test_ctx {
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
	char gdesc_buf[BV_TEST_BLK_SIZE] __aligned(8);
	char ind_buf[BV_TEST_BLK_SIZE] __aligned(8);

	struct ext4_group_desc gdps[BV_TEST_NGROUPS];
	journal_t mock_journal;

	struct bv_test_inode_slot slots[BV_TEST_MAX_SLOTS];

	/* Fault injection and call tracking */
	int fail_iget_err;
	unsigned int iget_calls;
	ext4_iget_flags last_iget_flags;

	int fail_map_err;
	unsigned int fail_map_on_call;
	unsigned int map_calls;
};

static inline ext4_fsblk_t bv_group_first_blk(ext4_group_t g)
{
	return BV_TEST_FIRST_DATA_BLK +
	       (ext4_fsblk_t)g * BV_TEST_BLOCKS_PER_GRP;
}

/*
 * Number of base metadata blocks in group @g under the default 4-group
 * sparse_super configuration (groups 0, 1, 3 have super + 1 gdb + 2 rsv_gdt;
 * group 2 has 0).
 */
static inline unsigned int bv_default_base_meta(ext4_group_t g)
{
	if (g == 2)
		return 0;
	return 1U + BV_TEST_GDB_COUNT + BV_TEST_RSV_GDT_BLKS;
}

static inline ext4_fsblk_t bv_default_bbitmap_blk(ext4_group_t g)
{
	return bv_group_first_blk(g) + bv_default_base_meta(g);
}

static inline ext4_fsblk_t bv_default_ibitmap_blk(ext4_group_t g)
{
	return bv_default_bbitmap_blk(g) + 1ULL;
}

static inline ext4_fsblk_t bv_default_itable_blk(ext4_group_t g)
{
	return bv_default_ibitmap_blk(g) + 1ULL;
}

static inline ext4_fsblk_t bv_default_first_free_blk(ext4_group_t g)
{
	return bv_default_itable_blk(g) + BV_TEST_ITB_PER_GRP;
}

static void bv_test_sb_upd_work(struct work_struct *work)
{
}

static void bv_test_err_timer(struct timer_list *t)
{
}

static unsigned long bv_dummy_shrinker_count(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	return 0;
}

static unsigned long bv_dummy_shrinker_scan(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	return SHRINK_STOP;
}

static void bv_test_init_ei_fields(struct ext4_inode_info *ei)
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
	ei->i_extra_isize = BV_TEST_EXTRA_ISIZE;
	ei->i_last_alloc_group = ~0U;
}

static struct inode *bv_test_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kzalloc_obj(*ei);
	if (!ei)
		return NULL;

	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
	bv_test_init_ei_fields(ei);
	return &ei->vfs_inode;
}

static void bv_test_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	ext4_clear_inode(inode);
}

static void bv_test_destroy_inode(struct inode *inode)
{
	kfree(ext4_i_metadata_bhs(inode));
	EXT4_I(inode)->i_metadata_bhs = NULL;
}

static void bv_test_free_inode_mem(struct inode *inode)
{
	kfree(EXT4_I(inode));
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **bv_test_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}
#endif

static const struct super_operations bv_test_sops = {
	.alloc_inode	= bv_test_alloc_inode,
	.evict_inode	= bv_test_evict_inode,
	.destroy_inode	= bv_test_destroy_inode,
	.free_inode	= bv_test_free_inode_mem,
	.drop_inode	= inode_just_drop,
#ifdef CONFIG_QUOTA
	.get_dquots	= bv_test_get_dquots,
#endif
};

static int bv_test_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static struct file_system_type bv_test_fs_type = {
	.name		 = "ext4_bv_kunit",
	.init_fs_context = bv_test_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

/* --- Helpers & Static Stubs --- */

static void bv_release_system_zone_locked(struct super_block *sb)
{
	down_write(&sb->s_umount);
	ext4_release_system_zone(sb);
	up_write(&sb->s_umount);
}

static struct bv_test_inode_slot *
bv_find_slot(struct bv_test_ctx *ctx, unsigned long ino)
{
	unsigned int i;

	for (i = 0; i < BV_TEST_MAX_SLOTS; i++) {
		if (ctx->slots[i].in_use && ctx->slots[i].ino == ino)
			return &ctx->slots[i];
	}
	return NULL;
}

static struct inode *bv_create_inode(struct kunit *test,
				     struct bv_test_ctx *ctx,
				     unsigned long ino, umode_t mode,
				     bool use_extents)
{
	struct bv_test_inode_slot *slot = NULL;
	struct ext4_extent_header *eh;
	struct inode *inode;
	unsigned int i;

	for (i = 0; i < BV_TEST_MAX_SLOTS; i++) {
		if (!ctx->slots[i].in_use) {
			slot = &ctx->slots[i];
			break;
		}
	}
	KUNIT_ASSERT_NOT_NULL(test, slot);

	inode = new_inode(ctx->sb);
	KUNIT_ASSERT_NOT_NULL(test, inode);

	slot->in_use = true;
	slot->ino = ino;
	slot->inode = inode;

	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_blkbits = ctx->sb->s_blocksize_bits;
	inode->i_flags |= S_NOQUOTA;
	set_nlink(inode, 1);
	EXT4_I(inode)->i_block_group = (ino - 1) / BV_TEST_INODES_PER_GRP;
	memset(EXT4_I(inode)->i_data, 0, sizeof(EXT4_I(inode)->i_data));

	if (use_extents) {
		ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
		eh = ext_inode_hdr(inode);
		eh->eh_magic = EXT4_EXT_MAGIC;
		eh->eh_entries = 0;
		eh->eh_max = cpu_to_le16((sizeof(EXT4_I(inode)->i_data) -
					  sizeof(*eh)) /
					 sizeof(struct ext4_extent));
		eh->eh_depth = 0;
	}

	return inode;
}

static void bv_append_extent(struct kunit *test, struct inode *inode,
			     ext4_lblk_t lblk, ext4_fsblk_t pblk,
			     unsigned short len)
{
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	u16 entries = le16_to_cpu(eh->eh_entries);
	struct ext4_extent *ex;

	KUNIT_ASSERT_LT(test, entries, le16_to_cpu(eh->eh_max));
	ex = EXT_FIRST_EXTENT(eh) + entries;
	ex->ee_block = cpu_to_le32(lblk);
	ex->ee_len = cpu_to_le16(len);
	ext4_ext_store_pblock(ex, pblk);
	eh->eh_entries = cpu_to_le16(entries + 1);
}

static void bv_set_group_meta(struct super_block *sb,
			      struct ext4_group_desc *gdp,
			      ext4_fsblk_t bbitmap, ext4_fsblk_t ibitmap,
			      ext4_fsblk_t itable)
{
	ext4_block_bitmap_set(sb, gdp, bbitmap);
	ext4_inode_bitmap_set(sb, gdp, ibitmap);
	ext4_inode_table_set(sb, gdp, itable);
}

static void bv_clear_fs_error(struct bv_test_ctx *ctx)
{
	ctx->sbi.s_mount_state &= ~EXT4_ERROR_FS;
	ctx->es.s_last_error_block = 0;
	ctx->es.s_last_error_ino = 0;
	ctx->es.s_last_error_errcode = 0;
}

static struct ext4_group_desc *
bv_get_group_desc_stub(struct super_block *sb, ext4_group_t block_group,
		       struct buffer_head **bh)
{
	struct kunit *test = kunit_get_current_test();
	struct bv_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx || block_group >= BV_TEST_NGROUPS)
		return NULL;
	if (bh)
		*bh = &ctx->gdesc_bh;
	return &ctx->gdps[block_group];
}

static struct inode *bv_iget_stub(struct super_block *sb, unsigned long ino,
				  ext4_iget_flags flags, const char *function,
				  unsigned int line)
{
	struct kunit *test = kunit_get_current_test();
	struct bv_test_ctx *ctx = test ? test->priv : NULL;
	struct bv_test_inode_slot *slot;

	if (!ctx)
		return ERR_PTR(-EIO);

	ctx->iget_calls++;
	ctx->last_iget_flags = flags;
	if (ctx->fail_iget_err)
		return ERR_PTR(ctx->fail_iget_err);

	slot = bv_find_slot(ctx, ino);
	if (!slot || !slot->inode)
		return ERR_PTR(-ENOENT);

	ihold(slot->inode);
	return slot->inode;
}

static int bv_map_blocks_fault_stub(handle_t *handle, struct inode *inode,
				    struct ext4_map_blocks *map, int flags)
{
	struct kunit *test = kunit_get_current_test();
	struct bv_test_ctx *ctx = test ? test->priv : NULL;

	if (!ctx)
		return -EIO;

	ctx->map_calls++;
	if (ctx->fail_map_err &&
	    (!ctx->fail_map_on_call ||
	     ctx->map_calls == ctx->fail_map_on_call))
		return ctx->fail_map_err;

	map->m_pblk = bv_default_first_free_blk(0) + map->m_lblk;
	map->m_len = 1;
	map->m_flags = EXT4_MAP_MAPPED;
	return 1;
}

/* --- Fixture Setup & Teardown --- */

static void bv_test_cleanup(struct bv_test_ctx *ctx)
{
	unsigned int i;

	ctx->sbi.s_journal = NULL;
	bv_release_system_zone_locked(ctx->sb);

	for (i = 0; i < BV_TEST_MAX_SLOTS; i++) {
		struct inode *inode = ctx->slots[i].inode;

		if (!inode)
			continue;
		spin_lock(&inode->i_lock);
		inode_state_clear(inode, I_DIRTY_TIME | I_NEW);
		spin_unlock(&inode->i_lock);
		set_nlink(inode, 1);
		iput(inode);
		ctx->slots[i].inode = NULL;
		ctx->slots[i].in_use = false;
	}

	deactivate_super(ctx->sb);
	ext4_es_unregister_shrinker(&ctx->sbi);
	ext4_es_destroy_stats(&ctx->sbi);
	rcu_barrier();

	cancel_work_sync(&ctx->sbi.s_sb_upd_work);
	timer_delete_sync(&ctx->sbi.s_err_report);
	free_percpu(ctx->bdev.bd_stats);
	kfree(ctx);
}

KUNIT_DEFINE_ACTION_WRAPPER(bv_test_cleanup_action, bv_test_cleanup,
			    struct bv_test_ctx *);

static int bv_test_init(struct kunit *test)
{
	struct bv_test_ctx *ctx;
	struct fs_context *fc;
	struct super_block *sb;
	struct ext4_sb_info *sbi;
	struct inode *root_inode;
	ext4_group_t g;
	int err;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	ctx->bdev.bd_stats = alloc_percpu(struct disk_stats);
	if (!ctx->bdev.bd_stats) {
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to alloc bd_stats");
	}

	fc = fs_context_for_mount(&bv_test_fs_type, 0);
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
	sb->s_op = &bv_test_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_flags |= SB_ACTIVE;
	sb->s_blocksize = BV_TEST_BLK_SIZE;
	sb->s_blocksize_bits = BV_TEST_BLK_BITS;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_xattr = ext4_xattr_handlers;
	strscpy(sb->s_id, "ext4_bv_kunit", sizeof(sb->s_id));

	inode_init_once(&ctx->bd_inode);
	ctx->bd_inode.i_sb = sb;
	ctx->bd_inode.i_blkbits = BV_TEST_BLK_BITS;
	ctx->bd_inode.i_mapping = &ctx->bd_inode.i_data;
	ctx->bd_inode.i_data.host = &ctx->bd_inode;
	ctx->bdev_queue.limits.logical_block_size = 512;
	ctx->bdev_queue.limits.dma_alignment = 511;
	ctx->bdev_disk.part0 = &ctx->bdev;
	ctx->bdev_disk.queue = &ctx->bdev_queue;
	ctx->bdev.bd_disk = &ctx->bdev_disk;
	ctx->bdev.bd_queue = &ctx->bdev_queue;
	ctx->bdev.bd_mapping = &ctx->bd_inode.i_data;
	bdev_clear_flag(&ctx->bdev, BD_READ_ONLY);
	sb->s_bdev = &ctx->bdev;

	bgl_lock_init(&ctx->bgl);
	sbi->s_blockgroup_lock = &ctx->bgl;

	sbi->s_es = &ctx->es;
	ctx->es.s_magic = cpu_to_le16(EXT4_SUPER_MAGIC);
	ctx->es.s_first_data_block = cpu_to_le32(BV_TEST_FIRST_DATA_BLK);
	ctx->es.s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ctx->es.s_creator_os = cpu_to_le32(EXT4_OS_LINUX);
	ctx->es.s_inodes_count = cpu_to_le32(BV_TEST_TOTAL_INODES);
	ctx->es.s_inodes_per_group = cpu_to_le32(BV_TEST_INODES_PER_GRP);
	ctx->es.s_blocks_per_group = cpu_to_le32(BV_TEST_BLOCKS_PER_GRP);
	ctx->es.s_clusters_per_group = cpu_to_le32(BV_TEST_BLOCKS_PER_GRP);
	ctx->es.s_first_ino = cpu_to_le32(BV_TEST_FIRST_INO);
	ctx->es.s_inode_size = cpu_to_le16(BV_TEST_INODE_SIZE);
	ctx->es.s_min_extra_isize = cpu_to_le16(BV_TEST_EXTRA_ISIZE);
	ctx->es.s_want_extra_isize = cpu_to_le16(BV_TEST_EXTRA_ISIZE);
	ctx->es.s_reserved_gdt_blocks = cpu_to_le16(BV_TEST_RSV_GDT_BLKS);
	ctx->es.s_checksum_type = EXT4_CRC32C_CHKSUM;
	ext4_blocks_count_set(&ctx->es, BV_TEST_TOTAL_BLKS);

	ext4_set_feature_sparse_super(sb);
	ext4_set_feature_extents(sb);
	ext4_set_feature_extra_isize(sb);
	ext4_set_feature_metadata_csum(sb);
	ext4_set_feature_64bit(sb);

	ctx->sb_bh.b_data = (char *)&ctx->es;
	ctx->sb_bh.b_size = sizeof(ctx->es);
	ctx->sb_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->sb_bh.b_count, 1);
	set_buffer_uptodate(&ctx->sb_bh);
	sbi->s_sbh = &ctx->sb_bh;

	ctx->gdesc_bh.b_data = ctx->gdesc_buf;
	ctx->gdesc_bh.b_size = BV_TEST_BLK_SIZE;
	ctx->gdesc_bh.b_blocknr = BV_TEST_GDESC_BLK;
	ctx->gdesc_bh.b_bdev = &ctx->bdev;
	atomic_set(&ctx->gdesc_bh.b_count, 1);
	set_buffer_uptodate(&ctx->gdesc_bh);

	sbi->s_inode_size = BV_TEST_INODE_SIZE;
	sbi->s_inodes_per_block = BV_TEST_INODES_PER_BLK;
	sbi->s_inodes_per_group = BV_TEST_INODES_PER_GRP;
	sbi->s_itb_per_group = BV_TEST_ITB_PER_GRP;
	sbi->s_first_ino = BV_TEST_FIRST_INO;
	sbi->s_want_extra_isize = BV_TEST_EXTRA_ISIZE;
	sbi->s_blocks_per_group = BV_TEST_BLOCKS_PER_GRP;
	sbi->s_clusters_per_group = BV_TEST_BLOCKS_PER_GRP;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_groups_count = BV_TEST_NGROUPS;
	sbi->s_gdb_count = BV_TEST_GDB_COUNT;
	sbi->s_desc_size = EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block = BV_TEST_BLK_SIZE / EXT4_MIN_DESC_SIZE_64BIT;
	sbi->s_desc_per_block_bits = ilog2(sbi->s_desc_per_block);
	sbi->s_bitmap_maxbytes = 0x1ff7ffff000ULL;

	mutex_init(&sbi->s_orphan_lock);
	INIT_LIST_HEAD(&sbi->s_orphan);
	set_opt(sb, ERRORS_CONT);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	INIT_WORK(&sbi->s_sb_upd_work, bv_test_sb_upd_work);
	timer_setup(&sbi->s_err_report, bv_test_err_timer, 0);
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

	for (g = 0; g < BV_TEST_NGROUPS; g++) {
		bv_set_group_meta(sb, &ctx->gdps[g],
				  bv_default_bbitmap_blk(g),
				  bv_default_ibitmap_blk(g),
				  bv_default_itable_blk(g));
	}

	err = ext4_es_register_shrinker(sbi);
	if (err) {
		deactivate_locked_super(sb);
		rcu_barrier();
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_ASSERT_EQ(test, err, 0);
	}
	WRITE_ONCE(sbi->s_es_shrinker->count_objects, bv_dummy_shrinker_count);
	WRITE_ONCE(sbi->s_es_shrinker->scan_objects, bv_dummy_shrinker_scan);

	root_inode = new_inode(sb);
	if (!root_inode) {
		deactivate_locked_super(sb);
		ext4_es_unregister_shrinker(sbi);
		ext4_es_destroy_stats(sbi);
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
		ext4_es_unregister_shrinker(sbi);
		ext4_es_destroy_stats(sbi);
		rcu_barrier();
		free_percpu(ctx->bdev.bd_stats);
		kfree(ctx);
		KUNIT_FAIL_AND_ABORT(test, "failed to create s_root");
	}

	up_write(&sb->s_umount);

	kunit_activate_static_stub(test, ext4_get_group_desc,
				   bv_get_group_desc_stub);
	kunit_activate_static_stub(test, __ext4_iget, bv_iget_stub);

	err = kunit_add_action_or_reset(test, bv_test_cleanup_action, ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	return 0;
}

/* --- Parameterized Test 1: Bounds & 64-bit Overflow Checks --- */

struct bv_bounds_case {
	const char *desc;
	u32 first_data_blk;
	ext4_fsblk_t total_blks;
	bool enable_64bit;
	ext4_fsblk_t start_blk;
	unsigned int count;
	int expected;
};

static const struct bv_bounds_case bv_bounds_cases[] = {
	{
		.desc = "1k_block_0_below_first_data",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 0,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "1k_block_1_equals_first_data",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 1,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "1k_block_2_just_above_first_data",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 2,
		.count = 1,
		.expected = 1,
	},
	{
		.desc = "4k_block_0_equals_first_data",
		.first_data_blk = 0,
		.total_blks = 4096,
		.enable_64bit = true,
		.start_blk = 0,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "4k_block_1_just_above_first_data",
		.first_data_blk = 0,
		.total_blks = 4096,
		.enable_64bit = true,
		.start_blk = 1,
		.count = 1,
		.expected = 1,
	},
	{
		.desc = "exact_last_single_block",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 4096,
		.count = 1,
		.expected = 1,
	},
	{
		.desc = "multiblock_ending_at_last_block",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 4093,
		.count = 4,
		.expected = 1,
	},
	{
		.desc = "multiblock_exceeding_end_by_one",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 4093,
		.count = 5,
		.expected = 0,
	},
	{
		.desc = "start_at_blocks_count",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = 4097,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "u64_wrap_around_into_in_bounds_value",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = U64_MAX - 3ULL,
		.count = 10,
		.expected = 0,
	},
	{
		.desc = "u64_max_single_block_overflow",
		.first_data_blk = 1,
		.total_blks = 4097,
		.enable_64bit = true,
		.start_blk = U64_MAX,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "64bit_high_block_at_exact_end",
		.first_data_blk = 0,
		.total_blks = (1ULL << 33) + 64ULL,
		.enable_64bit = true,
		.start_blk = (1ULL << 33) + 60ULL,
		.count = 4,
		.expected = 1,
	},
	{
		.desc = "32bit_fs_ignores_high_blocks_count",
		.first_data_blk = 0,
		.total_blks = (1ULL << 33) + 4096ULL,
		.enable_64bit = false,
		.start_blk = (1ULL << 33) + 10ULL,
		.count = 1,
		.expected = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(bv_bounds, bv_bounds_cases, desc);

static void test_sb_block_valid_bounds(struct kunit *test)
{
	const struct bv_bounds_case *tc = test->param_value;
	struct bv_test_ctx *ctx = test->priv;
	struct inode *inode;

	ctx->es.s_first_data_block = cpu_to_le32(tc->first_data_blk);
	ext4_blocks_count_set(&ctx->es, tc->total_blks);
	if (tc->enable_64bit)
		ext4_set_feature_64bit(ctx->sb);
	else
		ext4_clear_feature_64bit(ctx->sb);

	inode = bv_create_inode(test, ctx, 12, S_IFREG | 0644, true);

	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, tc->start_blk,
					    tc->count),
			tc->expected);
	KUNIT_EXPECT_EQ(test,
			ext4_inode_block_valid(inode, tc->start_blk, tc->count),
			tc->expected);
}

/* --- Test 2: Setup and Release Lifecycle (noblock_validity / RCU) --- */

static void test_release_system_zone_lifecycle(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	ext4_fsblk_t meta_blk = bv_default_bbitmap_blk(0);
	ext4_fsblk_t data_blk = bv_default_first_free_blk(0);

	/*
	 * Before ext4_setup_system_zone(), s_system_blks is NULL: releasing is
	 * a safe no-op and metadata block numbers pass validity as long as
	 * they are within (s_first_data_block, blocks_count].
	 */
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
	bv_release_system_zone_locked(ctx->sb);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, meta_blk, 1),
			1);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL,
					    BV_TEST_FIRST_DATA_BLK, 1),
			0);

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	KUNIT_EXPECT_NOT_NULL(test,
			      rcu_access_pointer(ctx->sbi.s_system_blks));
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, meta_blk, 1),
			0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, data_blk, 1),
			1);

	/*
	 * Releasing clears s_system_blks immediately and frees the rbtree via
	 * RCU; repeating release when already NULL remains a safe no-op.
	 */
	bv_release_system_zone_locked(ctx->sb);
	rcu_barrier();
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, meta_blk, 1),
			1);

	bv_release_system_zone_locked(ctx->sb);
}

/* --- Test 3: Multi-group sparse_super, flex_bg, and DEBUG Tree Print --- */

static void test_setup_multigroup_sparse_super_and_flex_bg(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	ext4_fsblk_t g2_bbitmap = 20ULL;
	ext4_fsblk_t g2_ibitmap = 22ULL;
	ext4_fsblk_t g2_itable = 30ULL;

	/*
	 * Relocate group 2's bitmaps and inode table into group 0 (flex_bg
	 * layout). Since group 2 also has 0 base metadata blocks under
	 * sparse_super, every block in group 2 (2049..3072) becomes valid for
	 * data, while group 0 gains three separated system zones.
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[2], g2_bbitmap, g2_ibitmap,
			  g2_itable);
	set_opt(ctx->sb, DEBUG);

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);

	/* Group 0 contiguous metadata [1..14] rejected; gap [15..19] valid */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 14, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 15, 5), 1);

	/* Relocated group 2 metadata inside group 0 and surrounding gaps */
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, g2_bbitmap, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 21, 1), 1);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, g2_ibitmap, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 23, 7), 1);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, g2_itable, 1), 0);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL,
					    g2_itable + BV_TEST_ITB_PER_GRP - 1,
					    1),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL,
					    g2_itable + BV_TEST_ITB_PER_GRP,
					    10),
			1);

	/* Group 1 [1025..1038] protected, 1024 and 1039 valid */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 1024, 1), 1);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 1025, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 1038, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 1039, 1), 1);

	/* Group 2 [2049..3072] has zero metadata blocks at all */
	KUNIT_EXPECT_EQ(test, ext4_num_base_meta_blocks(ctx->sb, 2), 0U);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL,
					    bv_group_first_blk(2),
					    BV_TEST_BLOCKS_PER_GRP),
			1);

	/* Group 3 (backup superblock 3^1): [3073..3086] protected, 3087+ ok */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 3073, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 3086, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 3087, 10), 1);
}

/* --- Test 4: Left, Right, and 3-Way Bridge Merging (+ 64-bit Zones) --- */

static void test_rbtree_left_right_and_bridge_merges(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	ext4_fsblk_t hi_base = (1ULL << 33) + 100ULL;

	ext4_blocks_count_set(&ctx->es, hi_base + 512ULL);

	/*
	 * Group 0: base_meta = [1..4], bbitmap = 5 (merges left -> [1..5]),
	 * ibitmap = 14 (standalone [14..14]), itable = [6..13] (bridges
	 * [1..5] on the left AND [14..14] on the right into [1..14]).
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[0], 5ULL, 14ULL, 6ULL);

	/*
	 * Group 2 (0 base meta): bbitmap = 2100, ibitmap = 2099 (inserted left
	 * of 2100 and merges right into [2099..2100]), itable = [2110..2117].
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[2], 2100ULL, 2099ULL, 2110ULL);

	/*
	 * Group 3: relocate bbitmap/ibitmap/itable above 2^32 to exercise
	 * 64-bit group descriptor blocks and 3-way merge above 4G blocks:
	 * bbitmap = hi_base, ibitmap = hi_base + 9, itable = hi_base + 1..8
	 * -> merged 10-block 64-bit zone [hi_base .. hi_base + 9].
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[3], hi_base, hi_base + 9ULL,
			  hi_base + 1ULL);

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);

	/* Group 0 bridged zone [1..14] */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 5, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 6, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 13, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 14, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 15, 1), 1);

	/* Group 2 right-merged zone [2099..2100] and separate [2110..2117] */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2098, 1), 1);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2099, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2100, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2101, 9), 1);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2110, 8), 0);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 2118, 1), 1);

	/* Group 3 64-bit bridged zone [hi_base .. hi_base + 9] */
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, hi_base - 1ULL, 1),
			1);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, hi_base, 1),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, hi_base + 5ULL, 1),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, hi_base + 9ULL, 1),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, hi_base + 10ULL, 4),
			1);
}

/* --- Parameterized Test 5: Interval Overlap Geometries --- */

/*
 * Tests against an isolated 8-block system zone [50, 58) (blocks 50..57)
 * surrounded by valid data blocks on both sides.
 */
struct bv_overlap_case {
	const char *desc;
	ext4_fsblk_t start_blk;
	unsigned int count;
	int expected;
};

static const struct bv_overlap_case bv_overlap_cases[] = {
	{
		.desc = "strictly_before_adjacent",
		.start_blk = 46,
		.count = 4,
		.expected = 1,
	},
	{
		.desc = "strictly_after_adjacent",
		.start_blk = 58,
		.count = 4,
		.expected = 1,
	},
	{
		.desc = "single_first_block_of_zone",
		.start_blk = 50,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "single_interior_block_of_zone",
		.start_blk = 54,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "single_last_block_of_zone",
		.start_blk = 57,
		.count = 1,
		.expected = 0,
	},
	{
		.desc = "straddle_left_boundary_by_one",
		.start_blk = 47,
		.count = 4,
		.expected = 0,
	},
	{
		.desc = "straddle_right_boundary_by_one",
		.start_blk = 57,
		.count = 4,
		.expected = 0,
	},
	{
		.desc = "strictly_inside_zone",
		.start_blk = 52,
		.count = 3,
		.expected = 0,
	},
	{
		.desc = "exact_zone_span",
		.start_blk = 50,
		.count = 8,
		.expected = 0,
	},
	{
		.desc = "strictly_enclosing_entire_zone",
		.start_blk = 45,
		.count = 20,
		.expected = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(bv_overlap, bv_overlap_cases, desc);

static void test_sb_block_valid_interval_overlaps(struct kunit *test)
{
	const struct bv_overlap_case *tc = test->param_value;
	struct bv_test_ctx *ctx = test->priv;
	struct inode *inode;

	/* Place group 2's 8-block inode table at [50..57] */
	bv_set_group_meta(ctx->sb, &ctx->gdps[2], 2049ULL, 2050ULL, 50ULL);
	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);

	inode = bv_create_inode(test, ctx, 13, S_IFREG | 0644, true);

	KUNIT_EXPECT_EQ(test,
			ext4_sb_block_valid(ctx->sb, NULL, tc->start_blk,
					    tc->count),
			tc->expected);
	KUNIT_EXPECT_EQ(test,
			ext4_inode_block_valid(inode, tc->start_blk, tc->count),
			tc->expected);
}

/* --- Parameterized Test 6: Corrupt Overlapping Group Metadata --- */

struct bv_corrupt_meta_case {
	const char *desc;
	ext4_group_t grp;
	ext4_fsblk_t bbitmap;
	ext4_fsblk_t ibitmap;
	ext4_fsblk_t itable;
};

static const struct bv_corrupt_meta_case bv_corrupt_meta_cases[] = {
	{
		.desc = "bbitmap_overlaps_group0_base_meta",
		.grp = 0,
		.bbitmap = 2,
		.ibitmap = 6,
		.itable = 7,
	},
	{
		.desc = "ibitmap_collides_with_bbitmap",
		.grp = 0,
		.bbitmap = 20,
		.ibitmap = 20,
		.itable = 30,
	},
	{
		.desc = "itable_collides_with_ibitmap",
		.grp = 0,
		.bbitmap = 20,
		.ibitmap = 25,
		.itable = 25,
	},
	{
		.desc = "group2_bbitmap_collides_with_group0_itable",
		.grp = 2,
		.bbitmap = 10,
		.ibitmap = 2050,
		.itable = 2051,
	},
	{
		.desc = "itable_starts_before_and_overlaps_bbitmap",
		.grp = 0,
		.bbitmap = 20,
		.ibitmap = 40,
		.itable = 15,
	},
	{
		.desc = "itable_starts_before_and_encloses_ibitmap",
		.grp = 2,
		.bbitmap = 50,
		.ibitmap = 55,
		.itable = 52,
	},
	{
		.desc = "itable_u64_wrap_around_overflow",
		.grp = 2,
		.bbitmap = 2049,
		.ibitmap = 2050,
		.itable = U64_MAX - 2ULL,
	},
};

KUNIT_ARRAY_PARAM_DESC(bv_corrupt_meta, bv_corrupt_meta_cases, desc);

static void test_setup_corrupt_group_metadata_overlaps(struct kunit *test)
{
	const struct bv_corrupt_meta_case *tc = test->param_value;
	struct bv_test_ctx *ctx = test->priv;

	bv_set_group_meta(ctx->sb, &ctx->gdps[tc->grp], tc->bbitmap,
			  tc->ibitmap, tc->itable);

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EFSCORRUPTED);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

/* --- Test 7: Journal Reserved Inode Ownership & can_merge Isolation --- */

static void test_journal_inode_protection_and_ino_isolation(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *j_inode;
	struct inode *reg_inode;

	/*
	 * Place group 2's block bitmap at block 20 (ino = 0) while group 0's
	 * metadata occupies [1..14] (ino = 0).
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[2], 20ULL, 2050ULL, 2051ULL);

	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);

	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, true);
	reg_inode = bv_create_inode(test, ctx, 14, S_IFREG | 0644, true);

	/*
	 * Non-block-aligned i_size (7 blocks + 1 byte -> rounds up to 8 logical
	 * blocks 0..7) with 4 real extents, an interior sparse hole at lblk 2,
	 * and a trailing sparse hole at lblk 7:
	 *  - lblk [0..1] -> pblk [15..16] (right-adjacent to [1..14] ino=0)
	 *  - lblk 2      -> sparse hole (n == 0)
	 *  - lblk [3..4] -> pblk [18..19] (left-adjacent to [20..20] ino=0)
	 *  - lblk 5      -> pblk [201..201] (ino=8)
	 *  - lblk 6      -> pblk [200..200] (merges right with 201 -> [200..201])
	 *  - lblk 7      -> sparse hole (n == 0)
	 */
	i_size_write(j_inode, 7 * BV_TEST_BLK_SIZE + 1);
	bv_append_extent(test, j_inode, 0, 15ULL, 2);
	bv_append_extent(test, j_inode, 3, 18ULL, 2);
	bv_append_extent(test, j_inode, 5, 201ULL, 1);
	bv_append_extent(test, j_inode, 6, 200ULL, 1);

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 1U);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->last_iget_flags & EXT4_IGET_SPECIAL));
	KUNIT_EXPECT_EQ(test, atomic_read(&j_inode->i_count), 1);

	/* Journal blocks are rejected for NULL and regular inodes */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 15, 2), 0);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(reg_inode, 15, 2), 0);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(reg_inode, 18, 2), 0);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(reg_inode, 200, 2), 0);

	/* Journal inode is permitted on all of its own extents */
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(j_inode, 15, 2), 1);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(j_inode, 18, 2), 1);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(j_inode, 200, 2), 1);

	/*
	 * Verify can_merge() kept ino=0 and ino=8 zones separate on both the
	 * left ([1..14] vs [15..16]) and right ([18..19] vs [20..20]): the
	 * journal inode must still be rejected on adjacent group metadata.
	 */
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(j_inode, 14, 1), 0);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(j_inode, 20, 1), 0);

	/* Unmapped physical gap block 17 remains valid for regular files */
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 17, 1), 1);
	KUNIT_EXPECT_EQ(test, ext4_inode_block_valid(reg_inode, 17, 1), 1);
}

/* --- Test 8: Disabled Journal Feature, Zero inum, and Empty Journal --- */

static void test_journal_disabled_or_empty_skips_zones(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *j_inode;

	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, true);
	i_size_write(j_inode, 2 * BV_TEST_BLK_SIZE);
	bv_append_extent(test, j_inode, 0, 100ULL, 2);

	/* Case 1: s_journal_inum set, but HAS_JOURNAL feature cleared */
	ext4_clear_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 100, 2), 1);
	bv_release_system_zone_locked(ctx->sb);

	/* Case 2: HAS_JOURNAL set, but s_journal_inum == 0 */
	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = 0;
	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	bv_release_system_zone_locked(ctx->sb);

	/* Case 3: HAS_JOURNAL + valid s_journal_inum, but i_size == 0 */
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	i_size_write(j_inode, 0);
	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 1U);
	KUNIT_EXPECT_EQ(test, atomic_read(&j_inode->i_count), 1);
	KUNIT_EXPECT_EQ(test, ext4_sb_block_valid(ctx->sb, NULL, 100, 2), 1);
}

/* --- Test 9: Journal Reserved Inode Error Paths --- */

static void test_journal_inode_invalid_inum(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;

	ext4_set_feature_journal(ctx->sb);

	/* Below EXT4_ROOT_INO (e.g. EXT4_BAD_INO = 1) */
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_BAD_INO);
	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));

	/* Above s_inodes_count */
	ctx->es.s_journal_inum = cpu_to_le32(BV_TEST_TOTAL_INODES + 1);
	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 0U);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

static void test_journal_inode_iget_error(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;

	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	ctx->fail_iget_err = -EIO;

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->iget_calls, 1U);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

static void test_journal_inode_map_blocks_error(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *j_inode;

	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, true);
	i_size_write(j_inode, 3 * BV_TEST_BLK_SIZE);

	/* Fail on 2nd ext4_map_blocks call after 1st extent was inserted */
	ctx->fail_map_err = -EIO;
	ctx->fail_map_on_call = 2;
	kunit_activate_static_stub(test, ext4_map_blocks,
				   bv_map_blocks_fault_stub);

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->map_calls, 2U);
	KUNIT_EXPECT_EQ(test, atomic_read(&j_inode->i_count), 1);
	KUNIT_EXPECT_FALSE(test,
			   (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

static void test_journal_inode_overlap_sets_fs_error(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *j_inode;

	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);
	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, true);
	i_size_write(j_inode, 2 * BV_TEST_BLK_SIZE);

	/* Extent at block 10 collides with group 0's inode table [7..14] */
	bv_append_extent(test, j_inode, 0, 10ULL, 2);

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_error_ino),
			(u32)EXT4_JOURNAL_INO);
	KUNIT_EXPECT_EQ(test, ctx->es.s_last_error_errcode,
			EXT4_ERR_EFSCORRUPTED);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

static void test_journal_extent_left_overlap_rejected(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *j_inode;

	/*
	 * Place group 2's block bitmap at block 20 (ino = 0) and map a
	 * multi-block journal extent at [18..21] that starts strictly before
	 * block 20 and extends across it. During ext4_setup_system_zone(),
	 * s_system_blks is still NULL when ext4_map_blocks() runs, so
	 * add_system_zone() must catch this left-traversal overlap.
	 */
	bv_set_group_meta(ctx->sb, &ctx->gdps[2], 20ULL, 2050ULL, 2051ULL);
	ext4_set_feature_journal(ctx->sb);
	ctx->es.s_journal_inum = cpu_to_le32(EXT4_JOURNAL_INO);

	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, true);
	i_size_write(j_inode, 4 * BV_TEST_BLK_SIZE);
	bv_append_extent(test, j_inode, 0, 18ULL, 4);

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_error_ino),
			(u32)EXT4_JOURNAL_INO);
	KUNIT_EXPECT_EQ(test, ctx->es.s_last_error_errcode,
			EXT4_ERR_EFSCORRUPTED);
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));

	/*
	 * Also verify a journal extent [1020..1040] that completely encloses
	 * group 1's entire metadata zone [1025..1038].
	 */
	bv_clear_fs_error(ctx);
	ext4_es_remove_extent(j_inode, 0, EXT_MAX_BLOCKS);
	ext_inode_hdr(j_inode)->eh_entries = 0;
	i_size_write(j_inode, 21 * BV_TEST_BLK_SIZE);
	bv_append_extent(test, j_inode, 0, 1020ULL, 21);

	KUNIT_EXPECT_EQ(test, ext4_setup_system_zone(ctx->sb), -EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_NULL(test, rcu_access_pointer(ctx->sbi.s_system_blks));
}

/* --- Test 10: ext4_check_blockref and Indirect Macros --- */

static void test_check_blockref_valid_and_sparse(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *inode;
	__le32 zero_refs[4] = { 0, 0, 0, 0 };
	__le32 mixed_refs[5] = {
		0,
		cpu_to_le32(15),
		0,
		cpu_to_le32(1039),
		cpu_to_le32(4096),
	};
	__le32 bad_first = cpu_to_le32(7);

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	inode = bv_create_inode(test, ctx, 15, S_IFREG | 0644, false);

	/* max == 0 does not inspect entries */
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, inode,
					    &bad_first, 0),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, inode,
					    zero_refs, ARRAY_SIZE(zero_refs)),
			0);
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, inode,
					    mixed_refs, ARRAY_SIZE(mixed_refs)),
			0);
	KUNIT_EXPECT_FALSE(test,
			   (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
}

static void
test_check_blockref_corrupt_entries_and_journal_bypass(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct inode *reg_inode;
	struct inode *j_inode;
	__le32 meta_refs[4] = {
		cpu_to_le32(15),
		0,
		cpu_to_le32(7),
		cpu_to_le32(9),
	};
	__le32 oob_refs[2] = {
		cpu_to_le32(16),
		cpu_to_le32(BV_TEST_TOTAL_BLKS),
	};

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	reg_inode = bv_create_inode(test, ctx, 16, S_IFREG | 0644, false);
	j_inode = bv_create_inode(test, ctx, EXT4_JOURNAL_INO,
				  S_IFREG | 0600, false);

	/* Stops at the first metadata block (7) and records error details */
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, reg_inode,
					    meta_refs, ARRAY_SIZE(meta_refs)),
			-EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_EQ(test, le64_to_cpu(ctx->es.s_last_error_block), 7ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_error_ino), 16U);

	/* Out-of-bounds block reference also triggers -EFSCORRUPTED */
	bv_clear_fs_error(ctx);
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, reg_inode,
					    oob_refs, ARRAY_SIZE(oob_refs)),
			-EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_EQ(test, le64_to_cpu(ctx->es.s_last_error_block),
			(u64)BV_TEST_TOTAL_BLKS);

	/* Active journal inode (s_journal->j_inode) bypasses blockref checks */
	bv_clear_fs_error(ctx);
	ctx->mock_journal.j_inode = j_inode;
	ctx->sbi.s_journal = &ctx->mock_journal;
	KUNIT_EXPECT_EQ(test,
			ext4_check_blockref(__func__, __LINE__, j_inode,
					    meta_refs, ARRAY_SIZE(meta_refs)),
			0);
	KUNIT_EXPECT_FALSE(test,
			   (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
}

static void test_ind_check_inode_and_indirect_blockref(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	unsigned int max_addrs = EXT4_ADDR_PER_BLOCK(ctx->sb);
	struct buffer_head ind_bh;
	struct inode *inode;
	__le32 *addrs;

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);
	inode = bv_create_inode(test, ctx, 17, S_IFREG | 0644, false);

	/* ext4_ind_check_inode() checks direct slots i_data[0..11] */
	EXT4_I(inode)->i_data[0] = cpu_to_le32(15);
	EXT4_I(inode)->i_data[EXT4_NDIR_BLOCKS - 1] = cpu_to_le32(20);
	KUNIT_EXPECT_EQ(test, ext4_ind_check_inode(inode), 0);

	EXT4_I(inode)->i_data[EXT4_NDIR_BLOCKS - 1] = cpu_to_le32(6);
	KUNIT_EXPECT_EQ(test, ext4_ind_check_inode(inode), -EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, le64_to_cpu(ctx->es.s_last_error_block), 6ULL);

	/*
	 * ext4_check_indirect_blockref() checks all EXT4_ADDR_PER_BLOCK(sb)
	 * entries (256 for 1KiB blocks), including the final slot.
	 */
	bv_clear_fs_error(ctx);
	memset(&ind_bh, 0, sizeof(ind_bh));
	memset(ctx->ind_buf, 0, sizeof(ctx->ind_buf));
	ind_bh.b_data = ctx->ind_buf;
	ind_bh.b_size = BV_TEST_BLK_SIZE;
	addrs = (__le32 *)ind_bh.b_data;

	addrs[0] = cpu_to_le32(15);
	addrs[max_addrs / 2] = cpu_to_le32(1039);
	addrs[max_addrs - 1] = cpu_to_le32(4096);
	KUNIT_EXPECT_EQ(test, ext4_check_indirect_blockref(inode, &ind_bh), 0);
	KUNIT_EXPECT_FALSE(test,
			   (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));

	addrs[max_addrs - 1] = cpu_to_le32(1029);
	KUNIT_EXPECT_EQ(test, ext4_check_indirect_blockref(inode, &ind_bh),
			-EFSCORRUPTED);
	KUNIT_EXPECT_EQ(test, le64_to_cpu(ctx->es.s_last_error_block), 1029ULL);
}

/* --- Test 11: Integration with ext4_map_blocks() Validity Check --- */

static void test_map_blocks_rejects_extent_in_system_zone(struct kunit *test)
{
	struct bv_test_ctx *ctx = test->priv;
	struct ext4_map_blocks map;
	struct inode *bad_inode;
	struct inode *ok_inode;

	KUNIT_ASSERT_EQ(test, ext4_setup_system_zone(ctx->sb), 0);

	ok_inode = bv_create_inode(test, ctx, 18, S_IFREG | 0644, true);
	bv_append_extent(test, ok_inode, 0, 25ULL, 3);
	memset(&map, 0, sizeof(map));
	map.m_lblk = 0;
	map.m_len = 3;
	KUNIT_EXPECT_EQ(test, ext4_map_blocks(NULL, ok_inode, &map, 0), 3);
	KUNIT_EXPECT_FALSE(test,
			   (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));

	/* Corrupt extent mapped onto group 0's inode table [7..14] */
	bad_inode = bv_create_inode(test, ctx, 19, S_IFREG | 0644, true);
	bv_append_extent(test, bad_inode, 0, 8ULL, 2);
	memset(&map, 0, sizeof(map));
	map.m_lblk = 0;
	map.m_len = 2;
	KUNIT_EXPECT_EQ(test, ext4_map_blocks(NULL, bad_inode, &map, 0),
			-EFSCORRUPTED);
	KUNIT_EXPECT_TRUE(test,
			  (bool)(ctx->sbi.s_mount_state & EXT4_ERROR_FS));
	KUNIT_EXPECT_EQ(test, le64_to_cpu(ctx->es.s_last_error_block), 8ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ctx->es.s_last_error_ino), 19U);
}

static struct kunit_case ext4_block_validity_test_cases[] = {
	KUNIT_CASE_PARAM(test_sb_block_valid_bounds, bv_bounds_gen_params),
	KUNIT_CASE(test_release_system_zone_lifecycle),
	KUNIT_CASE(test_setup_multigroup_sparse_super_and_flex_bg),
	KUNIT_CASE(test_rbtree_left_right_and_bridge_merges),
	KUNIT_CASE_PARAM(test_sb_block_valid_interval_overlaps,
			 bv_overlap_gen_params),
	KUNIT_CASE_PARAM(test_setup_corrupt_group_metadata_overlaps,
			 bv_corrupt_meta_gen_params),
	KUNIT_CASE(test_journal_inode_protection_and_ino_isolation),
	KUNIT_CASE(test_journal_disabled_or_empty_skips_zones),
	KUNIT_CASE(test_journal_inode_invalid_inum),
	KUNIT_CASE(test_journal_inode_iget_error),
	KUNIT_CASE(test_journal_inode_map_blocks_error),
	KUNIT_CASE(test_journal_inode_overlap_sets_fs_error),
	KUNIT_CASE(test_journal_extent_left_overlap_rejected),
	KUNIT_CASE(test_check_blockref_valid_and_sparse),
	KUNIT_CASE(test_check_blockref_corrupt_entries_and_journal_bypass),
	KUNIT_CASE(test_ind_check_inode_and_indirect_blockref),
	KUNIT_CASE(test_map_blocks_rejects_extent_in_system_zone),
	{}
};

static struct kunit_suite ext4_block_validity_test_suite = {
	.name = "ext4_block_validity_test",
	.init = bv_test_init,
	.test_cases = ext4_block_validity_test_cases,
};

kunit_test_suites(&ext4_block_validity_test_suite);

MODULE_DESCRIPTION("KUnit tests for ext4 block validity (system zone) tracking");
MODULE_LICENSE("GPL");
