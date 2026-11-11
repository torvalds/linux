// SPDX-License-Identifier: GPL-2.0
/*
 * Written by Ojaswin Mujoo <ojaswin@linux.ibm.com> (IBM)
 *
 * These Kunit tests are designed to test the functionality of
 * fs/ext4/extents.c, including extent tree initialization and validation,
 * binary search and multi-level navigation, extent insertion, merging,
 * splitting, tree depth growth and collapse, space removal and bigalloc
 * partial cluster handling, block mapping, unwritten-to-written conversion,
 * zeroout fallback, extent shifting, extent swapping, cluster mapping,
 * transaction credit calculation, and fast-commit replay helpers.
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/falloc.h>
#include <linux/fiemap.h>
#include <linux/fs_context.h>
#include <linux/gfp_types.h>
#include <linux/iversion.h>
#include <linux/stddef.h>

#include "ext4.h"
#include "ext4_extents.h"
#include "ext4_jbd2.h"

#define EXT_DATA_PBLK 100
#define EXT_DATA_LBLK 10
#define EXT_DATA_LEN 3

#define EXT_MAX_META_BLOCKS 16
#define EXT_META_BASE_PBLK  500
#define EXT_ALLOC_BASE_PBLK 1000
#define EXT_TOTAL_BLOCKS    0x100000ULL

struct kunit_meta_block {
	ext4_fsblk_t pblk;
	struct buffer_head bh;
	char data[4096];
	bool in_use;
};

static struct kunit_ctx {
	/*
	 * Ext4 inode initialized with 1 extent by default
	 */
	struct ext4_inode_info *k_ei;
	/*
	 * Optional secondary inode (used for swap_extents tests)
	 */
	struct ext4_inode_info *k_ei2;
	/*
	 * Represents the underlying data area (used for zeroout testing)
	 */
	char *k_data;
	/*
	 * In-memory pool of external extent tree blocks (leaf/index blocks)
	 */
	struct kunit_meta_block *meta_blocks;
	/*
	 * Mock block allocator and free tracking
	 */
	ext4_fsblk_t next_alloc_pblk;
	unsigned int alloc_calls;
	ext4_fsblk_t last_alloc_pblk;
	unsigned int last_alloc_len;
	int fail_alloc_err;

	unsigned int free_calls;
	ext4_fsblk_t last_free_pblk;
	unsigned long last_free_count;
	int last_free_flags;
	unsigned long total_freed_blocks;

	unsigned int zeroout_calls;
	ext4_lblk_t last_zeroout_lblk;
	ext4_lblk_t last_zeroout_len;
	unsigned int orphan_add_calls;
	unsigned int orphan_del_calls;
	struct dentry k_dentry;

	bool fail_getblk;
} k_ctx;

/*
 * describes the state of an expected extent in extent tree.
 */
struct kunit_ext_state {
	ext4_lblk_t ex_lblk;
	ext4_lblk_t ex_len;
	bool is_unwrit;
};

/*
 * describes the state of the data area of a writ extent. Used for testing
 * correctness of zeroout.
 */
struct kunit_ext_data_state {
	char exp_char;
	ext4_lblk_t off_blk;
	ext4_lblk_t len_blk;
};

enum kunit_test_types {
	TEST_SPLIT_CONVERT,
	TEST_CREATE_BLOCKS,
};

struct kunit_ext_test_param {
	/* description of test */
	char *desc;

	/* determines which function will be tested */
	int type;

	/* is extent unwrit at beginning of test */
	bool is_unwrit_at_start;

	/* flags to pass while splitting */
	int split_flags;

	/* map describing range to split */
	struct ext4_map_blocks split_map;

	/* disable zeroout */
	bool disable_zeroout;

	/* no of extents expected after split */
	int nr_exp_ext;

	/*
	 * expected state of extents after split. We will never split into more
	 * than 3 extents
	 */
	struct kunit_ext_state exp_ext_state[3];

	/* Below fields used for zeroout tests */

	bool is_zeroout_test;
	/*
	 * no of expected data segments (zeroout tests). Example, if we expect
	 * data to be 4kb 0s, followed by 8kb non-zero, then nr_exp_data_segs==2
	 */
	int nr_exp_data_segs;

	/*
	 * expected state of data area after zeroout.
	 */
	struct kunit_ext_data_state exp_data_state[3];
};

static unsigned long ext_dummy_shrinker_count(struct shrinker *shrink,
					      struct shrink_control *sc)
{
	return 0;
}

static unsigned long ext_dummy_shrinker_scan(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	return SHRINK_STOP;
}

#ifdef CONFIG_QUOTA
static struct dquot __rcu **ext_get_dquots(struct inode *inode)
{
	return EXT4_I(inode)->i_dquot;
}

static qsize_t *ext_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}

static const struct dquot_operations ext_dummy_qops = {
	.get_reserved_space = ext_get_reserved_space,
};
#endif

static const struct super_operations ext_dummy_sops = {
#ifdef CONFIG_QUOTA
	.get_dquots	= ext_get_dquots,
#endif
};

static int ext_init_fs_context(struct fs_context *fc)
{
	return 0;
}

static int ext_set(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static struct file_system_type ext_fs_type = {
	.name		 = "extents test",
	.init_fs_context = ext_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static void ext_init_mock_inode(struct ext4_inode_info *ei,
				struct super_block *sb, unsigned long ino)
{
	struct inode *inode = &ei->vfs_inode;

	memset(ei, 0, sizeof(*ei));
	inode->i_sb = sb;
	inode->i_ino = ino;
	inode->i_mode = S_IFREG | 0644;
	set_nlink(inode, 1);
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags |= S_NOQUOTA | S_NOSEC;
	inode->i_mapping = &inode->i_data;
	inode_set_iversion(inode, 1);

	spin_lock_init(&inode->i_lock);
	i_size_ordered_init(inode);
	init_rwsem(&inode->i_rwsem);
	address_space_init_once(&inode->i_data);
	init_rwsem(&inode->i_data.invalidate_lock);
	inode->i_data.host = inode;
	inode_state_set_raw(inode, I_DIRTY_SYNC | I_DIRTY_DATASYNC);

	ext4_es_init_tree(&ei->i_es_tree);
	ext4_init_pending_tree(&ei->i_pending_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_list);
	INIT_LIST_HEAD(&ei->i_orphan);
	INIT_LIST_HEAD(&ei->i_fc_list);
	INIT_LIST_HEAD(&ei->i_fc_dilist);
	spin_lock_init(&ei->i_fc_lock);
	init_rwsem(&ei->i_data_sem);
	init_rwsem(&ei->xattr_sem);
	spin_lock_init(&ei->i_raw_lock);
	spin_lock_init(&ei->i_block_reservation_lock);
	rwlock_init(&ei->i_prealloc_lock);
	ei->i_prealloc_node = RB_ROOT;
	ei->i_csum_seed = 0x12345678;

	ei->i_disksize = (EXT_DATA_LBLK + EXT_DATA_LEN + 10)
			 << sb->s_blocksize_bits;
	inode->i_size = ei->i_disksize;
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
}

static void ext_cleanup_inode(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct rb_node *node;

	down_write(&ei->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	up_write(&ei->i_data_sem);

	while ((node = rb_first(&ei->i_pending_tree.root)) != NULL) {
		struct pending_reservation *pr;

		pr = rb_entry(node, struct pending_reservation, rb_node);
		ext4_remove_pending(inode, EXT4_C2B(sbi, pr->lclu));
	}
}

static void extents_kunit_exit(struct kunit *test)
{
	struct block_device *bdev;
	struct ext4_sb_info *sbi;
	struct super_block *sb;

	if (!k_ctx.k_ei)
		return;

	sb = k_ctx.k_ei->vfs_inode.i_sb;
	sbi = sb->s_fs_info;
	bdev = sb->s_bdev;

	if (k_ctx.k_ei2) {
		ext_cleanup_inode(&k_ctx.k_ei2->vfs_inode);
		kfree(k_ctx.k_ei2);
	}
	ext_cleanup_inode(&k_ctx.k_ei->vfs_inode);

	percpu_counter_destroy(&sbi->s_freeclusters_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	ext4_es_unregister_shrinker(sbi);
	ext4_es_destroy_stats(sbi);
	deactivate_super(sb);

	kfree(k_ctx.meta_blocks);
	kfree(sbi->s_es);
	kfree(bdev->bd_queue);
	kfree(bdev->bd_mapping);
	kfree(bdev);
	kfree(sbi);
	kfree(k_ctx.k_ei);
	kfree(k_ctx.k_data);
	memset(&k_ctx, 0, sizeof(k_ctx));
}

static int __ext4_ext_dirty_stub(const char *where, unsigned int line,
				 handle_t *handle, struct inode *inode,
				 struct ext4_ext_path *path)
{
	if (path && path->p_bh) {
		ext4_extent_block_csum_set_test(inode,
						ext_block_hdr(path->p_bh));
		set_buffer_verified(path->p_bh);
	}
	return 0;
}

static int __ext4_handle_dirty_metadata_stub(const char *where,
					     unsigned int line,
					     handle_t *handle,
					     struct inode *inode,
					     struct buffer_head *bh)
{
	if (bh)
		set_buffer_uptodate(bh);
	return 0;
}

static int __ext4_mark_inode_dirty_stub(handle_t *handle, struct inode *inode,
					const char *func, unsigned int line)
{
	return 0;
}

static struct buffer_head *ext4_ext_getblk_stub(struct inode *inode,
						ext4_fsblk_t pblk,
						gfp_t gfp_flags)
{
	struct kunit_meta_block *slot = NULL;
	int i;

	if (k_ctx.fail_getblk || !k_ctx.meta_blocks)
		return NULL;

	for (i = 0; i < EXT_MAX_META_BLOCKS; i++) {
		if (k_ctx.meta_blocks[i].in_use &&
		    k_ctx.meta_blocks[i].pblk == pblk) {
			slot = &k_ctx.meta_blocks[i];
			break;
		}
	}

	if (!slot) {
		for (i = 0; i < EXT_MAX_META_BLOCKS; i++) {
			if (!k_ctx.meta_blocks[i].in_use) {
				slot = &k_ctx.meta_blocks[i];
				break;
			}
		}
		if (!slot)
			return NULL;

		memset(slot, 0, sizeof(*slot));
		slot->in_use = true;
		slot->pblk = pblk;
		slot->bh.b_data = slot->data;
		slot->bh.b_size = inode->i_sb->s_blocksize;
		slot->bh.b_blocknr = pblk;
		slot->bh.b_bdev = inode->i_sb->s_bdev;
		atomic_set(&slot->bh.b_count, 1);
		set_buffer_uptodate(&slot->bh);
		set_buffer_mapped(&slot->bh);
	}

	get_bh(&slot->bh);
	return &slot->bh;
}

static ext4_fsblk_t ext4_mb_new_blocks_stub(handle_t *handle,
					    struct ext4_allocation_request *ar,
					    int *errp)
{
	struct ext4_sb_info *sbi = EXT4_SB(ar->inode->i_sb);
	ext4_fsblk_t pblk;

	k_ctx.alloc_calls++;
	if (k_ctx.fail_alloc_err) {
		*errp = k_ctx.fail_alloc_err;
		return 0;
	}

	pblk = k_ctx.next_alloc_pblk;
	k_ctx.next_alloc_pblk += EXT4_C2B(sbi, ar->len);
	k_ctx.last_alloc_pblk = pblk;
	k_ctx.last_alloc_len = ar->len;
	*errp = 0;
	return pblk;
}

static void ext4_free_blocks_stub(handle_t *handle, struct inode *inode,
				  struct buffer_head *bh, ext4_fsblk_t block,
				  unsigned long count, int flags)
{
	k_ctx.free_calls++;
	k_ctx.last_free_pblk = block;
	k_ctx.last_free_count = count;
	k_ctx.last_free_flags = flags;
	k_ctx.total_freed_blocks += count;
}

static struct ext4_ext_path *
ext4_ext_insert_extent_stub(handle_t *handle, struct inode *inode,
			    struct ext4_ext_path *path,
			    struct ext4_extent *newext, int gb_flags)
{
	ext4_free_ext_path(path);
	return ERR_PTR(-ENOSPC);
}

/*
 * We will zeroout the equivalent range in the data area
 */
static int ext4_ext_zeroout_stub(handle_t *handle, struct inode *inode,
				 struct ext4_extent *ex)
{
	ext4_lblk_t ee_block, off_blk;
	loff_t ee_len;
	loff_t off_bytes;
	struct kunit *test = kunit_get_current_test();

	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);

	KUNIT_EXPECT_EQ_MSG(test, 1, ee_block >= EXT_DATA_LBLK, "ee_block=%d",
			    ee_block);
	KUNIT_EXPECT_EQ(test, 1,
			ee_block + ee_len <= EXT_DATA_LBLK + EXT_DATA_LEN);

	off_blk = ee_block - EXT_DATA_LBLK;
	off_bytes = off_blk << inode->i_sb->s_blocksize_bits;
	memset(k_ctx.k_data + off_bytes, 0,
	       ee_len << inode->i_sb->s_blocksize_bits);

	return 0;
}

static int ext4_issue_zeroout_stub(handle_t *handle, struct inode *inode,
				   ext4_lblk_t lblk, ext4_fsblk_t pblk,
				   ext4_lblk_t len)
{
	ext4_lblk_t off_blk;
	loff_t off_bytes;
	struct kunit *test = kunit_get_current_test();

	kunit_log(KERN_ALERT, test,
		  "%s: lblk=%u pblk=%llu len=%u", __func__, lblk, pblk, len);
	KUNIT_EXPECT_EQ(test, 1, lblk >= EXT_DATA_LBLK);
	KUNIT_EXPECT_EQ(test, 1, lblk + len <= EXT_DATA_LBLK + EXT_DATA_LEN);
	KUNIT_EXPECT_EQ(test, 1, lblk - EXT_DATA_LBLK == pblk - EXT_DATA_PBLK);

	off_blk = lblk - EXT_DATA_LBLK;
	off_bytes = off_blk << inode->i_sb->s_blocksize_bits;
	memset(k_ctx.k_data + off_bytes, 0,
	       len << inode->i_sb->s_blocksize_bits);

	return 0;
}

static int extents_kunit_init(struct kunit *test)
{
	struct ext4_extent_header *eh = NULL;
	struct ext4_inode_info *ei;
	struct inode *inode;
	struct super_block *sb;
	struct fs_context *fc;
	struct ext4_sb_info *sbi = NULL;
	int err;

	memset(&k_ctx, 0, sizeof(k_ctx));
	k_ctx.next_alloc_pblk = EXT_ALLOC_BASE_PBLK;

	sbi = kzalloc_obj(struct ext4_sb_info);
	if (sbi == NULL)
		return -ENOMEM;

	fc = fs_context_for_mount(&ext_fs_type, 0);
	if (IS_ERR(fc)) {
		kfree(sbi);
		return PTR_ERR(fc);
	}
	sb = sget_fc(fc, NULL, ext_set);
	put_fs_context(fc);
	if (IS_ERR(sb)) {
		kfree(sbi);
		return PTR_ERR(sb);
	}

	sbi->s_sb = sb;
	sb->s_fs_info = sbi;
	sb->s_op = &ext_dummy_sops;
#ifdef CONFIG_QUOTA
	sb->dq_op = &ext_dummy_qops;
#endif
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &ext4_cryptops;
#endif
	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = 12;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	strscpy(sb->s_id, "extents_kunit", sizeof(sb->s_id));

	sb->s_bdev = kzalloc_obj(struct block_device);
	if (!sb->s_bdev) {
		err = -ENOMEM;
		goto out_deactivate;
	}
	bdev_set_flag(sb->s_bdev, BD_READ_ONLY);
	sb->s_bdev->bd_mapping = kzalloc_obj(struct address_space);
	if (!sb->s_bdev->bd_mapping) {
		err = -ENOMEM;
		goto out_bdev;
	}
	sb->s_bdev->bd_queue = kzalloc_obj(struct request_queue);
	if (!sb->s_bdev->bd_queue) {
		err = -ENOMEM;
		goto out_bdev_mapping;
	}

	sbi->s_es = kzalloc_obj(struct ext4_super_block);
	if (!sbi->s_es) {
		err = -ENOMEM;
		goto out_bdev_queue;
	}
	sbi->s_es->s_first_data_block = cpu_to_le32(1);
	sbi->s_es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	ext4_blocks_count_set(sbi->s_es, EXT_TOTAL_BLOCKS);
	set_opt(sb, ERRORS_CONT);
	sbi->s_bitmap_maxbytes = 1ULL << 40;
	sbi->s_blocks_per_group = 32768;
	sbi->s_clusters_per_group = 32768;
	sbi->s_cluster_ratio = 1;
	sbi->s_cluster_bits = 0;
	sbi->s_desc_per_block = 64;
	sbi->s_groups_count = 32;
	sbi->s_extent_max_zeroout_kb = 32;
	mutex_init(&sbi->s_orphan_lock);
	INIT_LIST_HEAD(&sbi->s_orphan);
	spin_lock_init(&sbi->s_bdev_wb_lock);
	spin_lock_init(&sbi->s_error_lock);
	spin_lock_init(&sbi->s_md_lock);
	ratelimit_state_init(&sbi->s_err_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);
	ratelimit_state_init(&sbi->s_warning_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);
	ratelimit_state_init(&sbi->s_msg_ratelimit_state,
			     DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);

	err = percpu_counter_init(&sbi->s_freeclusters_counter, 100000,
				  GFP_KERNEL);
	if (err)
		goto out_es;
	err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0,
				  GFP_KERNEL);
	if (err)
		goto out_freeclusters;

	err = ext4_es_register_shrinker(sbi);
	if (err)
		goto out_dirtyclusters;

	/*
	 * Isolate the mock shrinker from asynchronous background reclaim.
	 */
	sbi->s_es_shrinker->count_objects = ext_dummy_shrinker_count;
	sbi->s_es_shrinker->scan_objects = ext_dummy_shrinker_scan;

	k_ctx.meta_blocks = kcalloc(EXT_MAX_META_BLOCKS,
				    sizeof(struct kunit_meta_block),
				    GFP_KERNEL);
	if (!k_ctx.meta_blocks) {
		err = -ENOMEM;
		goto out;
	}

	/* setup the mock inode */
	k_ctx.k_ei = kzalloc_obj(struct ext4_inode_info);
	if (k_ctx.k_ei == NULL) {
		err = -ENOMEM;
		goto out;
	}
	ei = k_ctx.k_ei;
	inode = &ei->vfs_inode;
	ext_init_mock_inode(ei, sb, 12);

	k_ctx.k_data = kzalloc(EXT_DATA_LEN * 4096, GFP_KERNEL);
	if (k_ctx.k_data == NULL) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * set the data area to a junk value
	 */
	memset(k_ctx.k_data, 'X', EXT_DATA_LEN * 4096);

	/* Fill extent header */
	eh = ext_inode_hdr(&k_ctx.k_ei->vfs_inode);
	eh->eh_depth = 0;
	eh->eh_entries = cpu_to_le16(1);
	eh->eh_magic = EXT4_EXT_MAGIC;
	eh->eh_max = cpu_to_le16(ext4_ext_space_root_idx_test(
					&k_ctx.k_ei->vfs_inode, 0));
	eh->eh_generation = 0;

	/*
	 * add 1 extent in leaf node covering:
	 * - lblks: [EXT_DATA_LBLK, EXT_DATA_LBLK + EXT_DATA_LEN)
	 * - pblks: [EXT_DATA_PBLK, EXT_DATA_PBLK + EXT_DATA_LEN)
	 */
	EXT_FIRST_EXTENT(eh)->ee_block = cpu_to_le32(EXT_DATA_LBLK);
	EXT_FIRST_EXTENT(eh)->ee_len = cpu_to_le16(EXT_DATA_LEN);
	ext4_ext_store_pblock(EXT_FIRST_EXTENT(eh), EXT_DATA_PBLK);
	ext4_ext_mark_unwritten(EXT_FIRST_EXTENT(eh));

	down_write(&ei->i_data_sem);
	ext4_es_insert_extent(inode, EXT_DATA_LBLK, EXT_DATA_LEN, EXT_DATA_PBLK,
			      EXTENT_STATUS_UNWRITTEN, 0);
	up_write(&ei->i_data_sem);

	/* Add stubs */
	kunit_activate_static_stub(test, __ext4_ext_dirty,
				   __ext4_ext_dirty_stub);
	kunit_activate_static_stub(test, __ext4_handle_dirty_metadata,
				   __ext4_handle_dirty_metadata_stub);
	kunit_activate_static_stub(test, __ext4_mark_inode_dirty,
				   __ext4_mark_inode_dirty_stub);
	kunit_activate_static_stub(test, ext4_ext_getblk,
				   ext4_ext_getblk_stub);
	kunit_activate_static_stub(test, ext4_mb_new_blocks,
				   ext4_mb_new_blocks_stub);
	kunit_activate_static_stub(test, ext4_free_blocks,
				   ext4_free_blocks_stub);
	kunit_activate_static_stub(test, ext4_ext_zeroout,
				   ext4_ext_zeroout_stub);
	kunit_activate_static_stub(test, ext4_issue_zeroout,
				   ext4_issue_zeroout_stub);
	up_write(&sb->s_umount);

	return 0;

out:
	kfree(k_ctx.k_data);
	k_ctx.k_data = NULL;
	kfree(k_ctx.k_ei);
	k_ctx.k_ei = NULL;
	kfree(k_ctx.meta_blocks);
	k_ctx.meta_blocks = NULL;
	ext4_es_unregister_shrinker(sbi);
	ext4_es_destroy_stats(sbi);
out_dirtyclusters:
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
out_freeclusters:
	percpu_counter_destroy(&sbi->s_freeclusters_counter);
out_es:
	kfree(sbi->s_es);
out_bdev_queue:
	kfree(sb->s_bdev->bd_queue);
out_bdev_mapping:
	kfree(sb->s_bdev->bd_mapping);
out_bdev:
	kfree(sb->s_bdev);
out_deactivate:
	deactivate_locked_super(sb);
	kfree(sbi);

	return err;
}

/*
 * Return 1 if all bytes in the buf equal to c, else return the offset of first mismatch
 */
static int check_buffer(char *buf, int c, int size)
{
	void *ret = NULL;

	ret = memchr_inv(buf, c, size);
	if (ret  == NULL)
		return 0;

	kunit_log(KERN_ALERT, kunit_get_current_test(),
		  "# %s: wrong char found at offset %u (expected:%d got:%d)", __func__,
		  (u32)((char *)ret - buf), c, *((char *)ret));
	return 1;
}

/*
 * Simulate a map block call by first calling ext4_map_query_blocks() to
 * correctly populate map flags and pblk and then call the
 * ext4_map_create_blocks() to do actual split and conversion. This is easier
 * than calling ext4_map_blocks() because that needs mocking a lot of unrelated
 * functions.
 */
static void ext4_map_create_blocks_helper(struct kunit *test,
					  struct inode *inode,
					  struct ext4_map_blocks *map,
					  int flags)
{
	int retval = 0;

	retval = ext4_map_query_blocks(NULL, inode, map, flags);
	if (retval < 0) {
		KUNIT_FAIL(test,
			   "ext4_map_query_blocks() failed. Cannot proceed\n");
		return;
	}

	ext4_map_create_blocks(NULL, inode, map, flags);
}

static void test_split_convert(struct kunit *test)
{
	struct ext4_ext_path *path;
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	struct ext4_map_blocks map;
	const struct kunit_ext_test_param *param =
		(const struct kunit_ext_test_param *)(test->param_value);
	int blkbits = inode->i_sb->s_blocksize_bits;

	if (param->disable_zeroout)
		EXT4_SB(inode->i_sb)->s_extent_max_zeroout_kb = 0;

	if (!param->is_unwrit_at_start) {
		ext4_ext_mark_initialized(EXT_FIRST_EXTENT(eh));
		down_write(&EXT4_I(inode)->i_data_sem);
		ext4_es_insert_extent(inode, EXT_DATA_LBLK, EXT_DATA_LEN,
				      EXT_DATA_PBLK, EXTENT_STATUS_WRITTEN, 0);
		up_write(&EXT4_I(inode)->i_data_sem);
	}

	if (param->is_zeroout_test)
		/*
		 * Force zeroout by making ext4_ext_insert_extent return ENOSPC
		 */
		kunit_activate_static_stub(test, ext4_ext_insert_extent,
					   ext4_ext_insert_extent_stub);

	path = ext4_find_extent(inode, EXT_DATA_LBLK, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	ex = path->p_ext;
	KUNIT_EXPECT_EQ(test, EXT_DATA_LBLK, le32_to_cpu(ex->ee_block));
	KUNIT_EXPECT_EQ(test, EXT_DATA_LEN, ext4_ext_get_actual_len(ex));
	KUNIT_EXPECT_EQ(test, param->is_unwrit_at_start,
			ext4_ext_is_unwritten(ex));
	if (param->is_zeroout_test)
		KUNIT_EXPECT_EQ(test, 0,
				check_buffer(k_ctx.k_data, 'X',
					     EXT_DATA_LEN << blkbits));

	map.m_lblk = param->split_map.m_lblk;
	map.m_len = param->split_map.m_len;

	switch (param->type) {
	case TEST_SPLIT_CONVERT:
		path = ext4_split_convert_extents_test(NULL, inode, &map,
					path, param->split_flags, NULL);
		if (!IS_ERR(path))
			ext4_free_ext_path(path);
		break;
	case TEST_CREATE_BLOCKS:
		ext4_free_ext_path(path);
		ext4_map_create_blocks_helper(test, inode, &map, param->split_flags);
		break;
	default:
		ext4_free_ext_path(path);
		KUNIT_FAIL(test, "param->type %d not support.", param->type);
		return;
	}

	path = ext4_find_extent(inode, EXT_DATA_LBLK, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	ex = path->p_ext;

	for (int i = 0; i < param->nr_exp_ext; i++) {
		struct kunit_ext_state exp_ext = param->exp_ext_state[i];
		bool es_check_needed = param->type != TEST_SPLIT_CONVERT;
		struct extent_status es;
		int contains_ex, ex_end, es_end, es_pblk;

		KUNIT_EXPECT_EQ(test, exp_ext.ex_lblk,
				le32_to_cpu(ex->ee_block));
		KUNIT_EXPECT_EQ(test, exp_ext.ex_len,
				ext4_ext_get_actual_len(ex));
		KUNIT_EXPECT_EQ(test, exp_ext.is_unwrit,
				ext4_ext_is_unwritten(ex));
		/*
		 * Confirm extent cache is in sync. Note that es cache can be
		 * merged even when on-disk extents are not so take that into
		 * account.
		 *
		 * Also, ext4_split_convert_extents() forces EXT4_EX_NOCACHE hence
		 * es status are ignored for that case.
		 */
		if (es_check_needed) {
			ext4_es_lookup_extent(inode, le32_to_cpu(ex->ee_block),
					      NULL, &es, NULL);

			ex_end = exp_ext.ex_lblk + exp_ext.ex_len;
			es_end = es.es_lblk + es.es_len;
			contains_ex = es.es_lblk <= exp_ext.ex_lblk &&
				      es_end >= ex_end;
			es_pblk = ext4_es_pblock(&es) +
				  (exp_ext.ex_lblk - es.es_lblk);

			KUNIT_EXPECT_EQ(test, contains_ex, 1);
			KUNIT_EXPECT_EQ(test, ext4_ext_pblock(ex), es_pblk);
			KUNIT_EXPECT_EQ(test, 1,
					(exp_ext.is_unwrit &&
					 ext4_es_is_unwritten(&es)) ||
						(!exp_ext.is_unwrit &&
						 ext4_es_is_written(&es)));
		}

		/* Only printed on failure */
		kunit_log(KERN_INFO, test,
			  "# [extent %d] exp: lblk:%d len:%d unwrit:%d \n", i,
			  exp_ext.ex_lblk, exp_ext.ex_len, exp_ext.is_unwrit);
		kunit_log(KERN_INFO, test,
			  "# [extent %d] got: lblk:%d len:%d unwrit:%d\n", i,
			  le32_to_cpu(ex->ee_block),
			  ext4_ext_get_actual_len(ex),
			  ext4_ext_is_unwritten(ex));
		if (es_check_needed)
			kunit_log(
				KERN_INFO, test,
				"# [extent %d] es: lblk:%d len:%d pblk:%lld type:0x%x\n",
				i, es.es_lblk, es.es_len, ext4_es_pblock(&es),
				ext4_es_type(&es));
		kunit_log(KERN_INFO, test, "------------------\n");

		ex = ex + 1;
	}

	ext4_free_ext_path(path);

	if (!param->is_zeroout_test)
		return;

	/*
	 * Check that then data area has been zeroed out correctly
	 */
	for (int i = 0; i < param->nr_exp_data_segs; i++) {
		loff_t off, len;
		struct kunit_ext_data_state exp_data_seg = param->exp_data_state[i];

		off = exp_data_seg.off_blk << blkbits;
		len = exp_data_seg.len_blk << blkbits;
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    check_buffer(k_ctx.k_data + off,
						 exp_data_seg.exp_char, len),
				    "# corruption in byte range [%lld, %lld)",
				    off, len);
	}
}

/*
 * Helper to reset an inode's extent tree and extent status cache to empty.
 */
static void ext_reset_tree(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	down_write(&ei->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	memset(ei->i_data, 0, sizeof(ei->i_data));
	ext4_ext_tree_init(NULL, inode);
	up_write(&ei->i_data_sem);
}

/*
 * Helper to insert an extent into @inode's extent tree under i_data_sem.
 */
static int ext_insert_one(struct inode *inode, ext4_lblk_t lblk,
			  unsigned short len, ext4_fsblk_t pblk,
			  bool unwritten, int gb_flags)
{
	struct ext4_ext_path *path;
	struct ext4_extent newex = {};

	newex.ee_block = cpu_to_le32(lblk);
	newex.ee_len = cpu_to_le16(len);
	ext4_ext_store_pblock(&newex, pblk);
	if (unwritten)
		ext4_ext_mark_unwritten(&newex);

	down_write(&EXT4_I(inode)->i_data_sem);
	path = ext4_find_extent(inode, lblk, NULL, EXT4_EX_NOCACHE);
	if (IS_ERR(path)) {
		up_write(&EXT4_I(inode)->i_data_sem);
		return PTR_ERR(path);
	}
	path = ext4_ext_insert_extent(NULL, inode, path, &newex,
				      gb_flags | EXT4_EX_NOCACHE);
	up_write(&EXT4_I(inode)->i_data_sem);
	if (IS_ERR(path))
		return PTR_ERR(path);

	ext4_free_ext_path(path);
	return 0;
}

/*
 * Verify 48-bit physical block number store and load helpers for both
 * leaf extents (ext4_extent) and index entries (ext4_extent_idx).
 */
static void test_ext_pblock_48bit(struct kunit *test)
{
	struct ext4_extent ex = {};
	struct ext4_extent_idx idx = {};
	const ext4_fsblk_t pblk_48 = 0x123456789abcULL;
	const ext4_fsblk_t pblk_max = 0xffffffffffffULL;

	ext4_ext_store_pblock(&ex, 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex), 0ULL);

	ext4_ext_store_pblock(&ex, 0xffffffffULL);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex), 0xffffffffULL);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ex.ee_start_hi), 0);

	ext4_ext_store_pblock(&ex, pblk_48);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex.ee_start_lo), 0x56789abcU);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ex.ee_start_hi), 0x1234U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex), pblk_48);

	ext4_ext_store_pblock(&ex, pblk_max);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex), pblk_max);

	ext4_idx_store_pblock(&idx, pblk_48);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(idx.ei_leaf_lo), 0x56789abcU);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(idx.ei_leaf_hi), 0x1234U);
	KUNIT_EXPECT_EQ(test, ext4_idx_pblock(&idx), pblk_48);

	ext4_idx_store_pblock(&idx, pblk_max);
	KUNIT_EXPECT_EQ(test, ext4_idx_pblock(&idx), pblk_max);
}

/*
 * Verify extent length encoding and unwritten bit manipulation at boundaries:
 * - EXT_INIT_MAX_LEN (32768 = 0x8000) is the maximum initialized extent length
 *   and must NOT be treated as unwritten.
 * - EXT_UNWRITTEN_MAX_LEN (32767 = 0x7fff) is the maximum unwritten extent
 *   length (stored on disk as 0xffff).
 */
static void test_ext_unwritten_len_encoding(struct kunit *test)
{
	struct ext4_extent ex = {};

	ex.ee_len = cpu_to_le16(1);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex), 1);

	ext4_ext_mark_unwritten(&ex);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 1);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex), 1);

	ext4_ext_mark_initialized(&ex);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex), 1);

	ex.ee_len = cpu_to_le16(EXT_UNWRITTEN_MAX_LEN);
	ext4_ext_mark_unwritten(&ex);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 1);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex),
			EXT_UNWRITTEN_MAX_LEN);

	ext4_ext_mark_initialized(&ex);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex),
			EXT_UNWRITTEN_MAX_LEN);

	ex.ee_len = cpu_to_le16(EXT_INIT_MAX_LEN);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex), 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex), EXT_INIT_MAX_LEN);
}

/*
 * Verify ext4_ext_tree_init() initializes the in-inode extent header and
 * ext4_ext_check_inode() accepts valid empty and populated root headers.
 */
static void test_ext_tree_init_and_check_inode(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);

	memset(k_ctx.k_ei->i_data, 0xff, sizeof(k_ctx.k_ei->i_data));
	ext4_ext_tree_init(NULL, inode);

	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_magic),
			le16_to_cpu(EXT4_EXT_MAGIC));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_depth), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_max), 4);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(eh->eh_generation), 0U);
	KUNIT_EXPECT_EQ(test, ext4_ext_check_inode(inode), 0);

	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 5, 100, false, 0), 0);
	KUNIT_EXPECT_EQ(test, ext4_ext_check_inode(inode), 0);
}

enum ext_check_corrupt_type {
	CORRUPT_BAD_MAGIC,
	CORRUPT_BAD_DEPTH,
	CORRUPT_ZERO_MAX,
	CORRUPT_EXCESS_MAX,
	CORRUPT_ENTRIES_GT_MAX,
	CORRUPT_EMPTY_NONROOT_INDEX,
	CORRUPT_ZERO_LEN_EXTENT,
	CORRUPT_LBLK_OVERFLOW,
	CORRUPT_PBLK_TOO_LOW,
	CORRUPT_PBLK_TOO_HIGH,
	CORRUPT_UNSORTED_EXTENTS,
	CORRUPT_UNSORTED_INDEXES,
	CORRUPT_NONROOT_LBLK_MISMATCH,
};

struct ext_check_corrupt_param {
	const char *desc;
	enum ext_check_corrupt_type type;
};

static const struct ext_check_corrupt_param ext_check_corrupt_params[] = {
	{ "invalid eh_magic", CORRUPT_BAD_MAGIC },
	{ "mismatched eh_depth", CORRUPT_BAD_DEPTH },
	{ "eh_max == 0", CORRUPT_ZERO_MAX },
	{ "eh_max exceeds root capacity", CORRUPT_EXCESS_MAX },
	{ "eh_entries exceeds eh_max", CORRUPT_ENTRIES_GT_MAX },
	{ "empty index block at depth > 0", CORRUPT_EMPTY_NONROOT_INDEX },
	{ "extent with ee_len == 0", CORRUPT_ZERO_LEN_EXTENT },
	{ "extent logical block 32-bit wrap-around", CORRUPT_LBLK_OVERFLOW },
	{ "extent physical block <= first_data_block", CORRUPT_PBLK_TOO_LOW },
	{ "extent physical block past blocks_count", CORRUPT_PBLK_TOO_HIGH },
	{ "overlapping/unsorted leaf extents", CORRUPT_UNSORTED_EXTENTS },
	{ "unsorted index entries", CORRUPT_UNSORTED_INDEXES },
	{ "non-root leaf first ee_block != parent ei_block",
	  CORRUPT_NONROOT_LBLK_MISMATCH },
};

static void ext_check_corrupt_get_desc(struct kunit *test, const void *p,
				       char *desc)
{
	const struct ext_check_corrupt_param *param = p;

	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}

static int ext_check_corrupt_param_init(struct kunit *test)
{
	kunit_register_params_array(test, ext_check_corrupt_params,
				    ARRAY_SIZE(ext_check_corrupt_params),
				    ext_check_corrupt_get_desc);
	return 0;
}

/*
 * Verify __ext4_ext_check() detects and rejects all classes of corrupt
 * extent headers, leaf extents, and index entries with -EFSCORRUPTED.
 */
static void test_ext_check_corruptions(struct kunit *test)
{
	const struct ext_check_corrupt_param *param = test->param_value;
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	struct ext4_extent_idx *ix;
	struct {
		struct ext4_extent_header eh;
		struct ext4_extent ex;
	} nonroot_leaf;
	int depth = 0;
	ext4_fsblk_t pblk = 0;
	ext4_lblk_t lblk = 0;
	int err;

	ext_reset_tree(inode);
	eh->eh_entries = cpu_to_le16(1);
	ex = EXT_FIRST_EXTENT(eh);
	ex->ee_block = cpu_to_le32(10);
	ex->ee_len = cpu_to_le16(4);
	ext4_ext_store_pblock(ex, 100);

	switch (param->type) {
	case CORRUPT_BAD_MAGIC:
		eh->eh_magic = cpu_to_le16(0xdead);
		break;
	case CORRUPT_BAD_DEPTH:
		eh->eh_depth = cpu_to_le16(1);
		depth = 0;
		break;
	case CORRUPT_ZERO_MAX:
		eh->eh_max = 0;
		break;
	case CORRUPT_EXCESS_MAX:
		eh->eh_max = cpu_to_le16(10);
		break;
	case CORRUPT_ENTRIES_GT_MAX:
		eh->eh_entries = cpu_to_le16(5);
		break;
	case CORRUPT_EMPTY_NONROOT_INDEX:
		eh->eh_depth = cpu_to_le16(1);
		eh->eh_entries = 0;
		depth = 1;
		break;
	case CORRUPT_ZERO_LEN_EXTENT:
		ex->ee_len = 0;
		break;
	case CORRUPT_LBLK_OVERFLOW:
		ex->ee_block = cpu_to_le32(0xfffffff0U);
		ex->ee_len = cpu_to_le16(32);
		break;
	case CORRUPT_PBLK_TOO_LOW:
		ext4_ext_store_pblock(ex, 1);
		break;
	case CORRUPT_PBLK_TOO_HIGH:
		ext4_ext_store_pblock(ex, EXT_TOTAL_BLOCKS);
		break;
	case CORRUPT_UNSORTED_EXTENTS:
		eh->eh_entries = cpu_to_le16(2);
		ex[1].ee_block = cpu_to_le32(12);
		ex[1].ee_len = cpu_to_le16(4);
		ext4_ext_store_pblock(&ex[1], 200);
		break;
	case CORRUPT_UNSORTED_INDEXES:
		eh->eh_depth = cpu_to_le16(1);
		eh->eh_entries = cpu_to_le16(2);
		depth = 1;
		ix = EXT_FIRST_INDEX(eh);
		ix[0].ei_block = cpu_to_le32(20);
		ext4_idx_store_pblock(&ix[0], 500);
		ix[1].ei_block = cpu_to_le32(10);
		ext4_idx_store_pblock(&ix[1], 501);
		break;
	case CORRUPT_NONROOT_LBLK_MISMATCH:
		eh->eh_depth = cpu_to_le16(1);
		memset(&nonroot_leaf, 0, sizeof(nonroot_leaf));
		nonroot_leaf.eh.eh_magic = EXT4_EXT_MAGIC;
		nonroot_leaf.eh.eh_depth = 0;
		nonroot_leaf.eh.eh_entries = cpu_to_le16(1);
		nonroot_leaf.eh.eh_max = cpu_to_le16(340);
		nonroot_leaf.ex.ee_block = cpu_to_le32(10);
		nonroot_leaf.ex.ee_len = cpu_to_le16(4);
		ext4_ext_store_pblock(&nonroot_leaf.ex, 100);
		eh = &nonroot_leaf.eh;
		depth = 0;
		pblk = 500;
		lblk = 20;
		break;
	}

	/* Suppress __ext4_error_inode() log spam during expected failure */
	set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
	err = ext4_ext_check_test(inode, eh, depth, pblk, lblk);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);

	KUNIT_EXPECT_EQ(test, err, -EFSCORRUPTED);
}

/*
 * Verify metadata checksum calculation and verification on external extent
 * tree blocks when EXT4_FEATURE_RO_COMPAT_METADATA_CSUM is enabled.
 */
static void test_ext_block_csum_verify(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct buffer_head *bh;
	struct ext4_extent_header *eh;
	struct ext4_extent *ex;
	struct ext4_extent_tail *et;
	int max_entries;
	int err;

	ext_reset_tree(inode);
	ext_inode_hdr(inode)->eh_depth = cpu_to_le16(1);
	ext4_set_feature_metadata_csum(inode->i_sb);
	bh = ext4_ext_getblk_stub(inode, EXT_META_BASE_PBLK, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bh);

	max_entries = (inode->i_sb->s_blocksize -
		       sizeof(struct ext4_extent_header) -
		       sizeof(struct ext4_extent_tail)) /
		      sizeof(struct ext4_extent);

	eh = ext_block_hdr(bh);
	eh->eh_magic = EXT4_EXT_MAGIC;
	eh->eh_depth = 0;
	eh->eh_entries = cpu_to_le16(1);
	eh->eh_max = cpu_to_le16(max_entries);

	ex = EXT_FIRST_EXTENT(eh);
	ex->ee_block = cpu_to_le32(10);
	ex->ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(ex, 100);

	ext4_extent_block_csum_set_test(inode, eh);
	err = ext4_ext_check_test(inode, eh, 0, EXT_META_BASE_PBLK, 10);
	KUNIT_EXPECT_EQ(test, err, 0);

	/* Flip a bit in the tail checksum and verify -EFSBADCRC */
	et = find_ext4_extent_tail(eh);
	et->et_checksum ^= cpu_to_le32(0x1);

	set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
	err = ext4_ext_check_test(inode, eh, 0, EXT_META_BASE_PBLK, 10);
	clear_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);

	KUNIT_EXPECT_EQ(test, err, -EFSBADCRC);
	brelse(bh);
}

/*
 * Verify ext4_find_extent() binary search on an empty tree and a multi-extent
 * root leaf, as well as extent status caching vs EXT4_EX_NOCACHE.
 */
static void test_find_extent_root_binsearch(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_ext_path *path;
	struct extent_status es;

	ext_reset_tree(inode);

	/* Empty tree returns NULL p_ext */
	path = ext4_find_extent(inode, 10, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_PTR_EQ(test, path[0].p_ext, NULL);
	ext4_free_ext_path(path);

	/* Populate 3 separated extents: [10, 15), [20, 25), [30, 35) */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 5, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 20, 5, 200, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 30, 5, 300, false, 0), 0);

	/* Lookup before first extent (lblk 5) -> points to first extent */
	path = ext4_find_extent(inode, 5, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_ASSERT_NOT_NULL(test, path[0].p_ext);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[0].p_ext->ee_block), 10U);
	ext4_free_ext_path(path);

	/* Lookup inside first extent (lblk 12) */
	path = ext4_find_extent(inode, 12, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[0].p_ext->ee_block), 10U);
	ext4_free_ext_path(path);

	/* Lookup in hole between 1st and 2nd extent (lblk 17) -> 1st extent */
	path = ext4_find_extent(inode, 17, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[0].p_ext->ee_block), 10U);
	ext4_free_ext_path(path);

	/* Lookup exact start of 2nd extent (lblk 20) */
	path = ext4_find_extent(inode, 20, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[0].p_ext->ee_block), 20U);
	ext4_free_ext_path(path);

	/* Lookup past last extent (lblk 50) -> 3rd extent */
	path = ext4_find_extent(inode, 50, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[0].p_ext->ee_block), 30U);
	ext4_free_ext_path(path);

	/* EXT4_EX_NOCACHE left i_es_tree empty */
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 10, NULL, &es, NULL),
			0);

	/* Lookup without EXT4_EX_NOCACHE populates extents and holes in es */
	down_read(&EXT4_I(inode)->i_data_sem);
	path = ext4_find_extent(inode, 20, NULL, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	ext4_free_ext_path(path);

	/*
	 * Verify inter-extent hole [15, 20) and extents are cached in es
	 * (ext4_cache_extents only caches holes where prev != 0).
	 */
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 5, NULL, &es, NULL),
			0);
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 17, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_hole(&es));
	KUNIT_EXPECT_EQ(test, es.es_lblk, 15U);
	KUNIT_EXPECT_EQ(test, es.es_len, 5U);

	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 22, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_written(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 200ULL);
}

/*
 * Helper to construct a depth-1 tree with two external leaf blocks:
 *   Leaf 0 (pblk 500): [10, 15) -> 1000 (written), [20, 25) -> 1100 (written)
 *   Leaf 1 (pblk 501): [40, 45) -> 2000 (unwritten), [50, 55) -> 2100 (written)
 */
static void setup_depth1_two_leaf_tree(struct kunit *test, struct inode *inode)
{
	struct ext4_extent_header *root_eh = ext_inode_hdr(inode);
	struct ext4_extent_idx *idx;
	struct buffer_head *bh0, *bh1;
	struct ext4_extent_header *eh0, *eh1;
	struct ext4_extent *ex;
	int max_leaf = (inode->i_sb->s_blocksize -
			sizeof(struct ext4_extent_header)) /
		       sizeof(struct ext4_extent);

	ext_reset_tree(inode);

	root_eh->eh_depth = cpu_to_le16(1);
	root_eh->eh_entries = cpu_to_le16(2);
	root_eh->eh_max = cpu_to_le16(ext4_ext_space_root_idx_test(inode, 0));

	idx = EXT_FIRST_INDEX(root_eh);
	idx[0].ei_block = cpu_to_le32(10);
	ext4_idx_store_pblock(&idx[0], EXT_META_BASE_PBLK);
	idx[1].ei_block = cpu_to_le32(40);
	ext4_idx_store_pblock(&idx[1], EXT_META_BASE_PBLK + 1);

	bh0 = ext4_ext_getblk_stub(inode, EXT_META_BASE_PBLK, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bh0);
	eh0 = ext_block_hdr(bh0);
	eh0->eh_magic = EXT4_EXT_MAGIC;
	eh0->eh_depth = 0;
	eh0->eh_entries = cpu_to_le16(2);
	eh0->eh_max = cpu_to_le16(max_leaf);
	ex = EXT_FIRST_EXTENT(eh0);
	ex[0].ee_block = cpu_to_le32(10);
	ex[0].ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex[0], 1000);
	ex[1].ee_block = cpu_to_le32(20);
	ex[1].ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex[1], 1100);
	brelse(bh0);

	bh1 = ext4_ext_getblk_stub(inode, EXT_META_BASE_PBLK + 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bh1);
	eh1 = ext_block_hdr(bh1);
	eh1->eh_magic = EXT4_EXT_MAGIC;
	eh1->eh_depth = 0;
	eh1->eh_entries = cpu_to_le16(2);
	eh1->eh_max = cpu_to_le16(max_leaf);
	ex = EXT_FIRST_EXTENT(eh1);
	ex[0].ee_block = cpu_to_le32(40);
	ex[0].ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex[0], 2000);
	ext4_ext_mark_unwritten(&ex[0]);
	ex[1].ee_block = cpu_to_le32(50);
	ex[1].ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex[1], 2100);
	brelse(bh1);
}

/*
 * Verify multi-level (depth 1) extent lookup and tree navigation helpers:
 * ext4_ext_next_allocated_block(), ext4_ext_next_leaf_block(),
 * ext4_ext_search_left(), and ext4_ext_search_right() across leaf boundaries.
 */
static void test_find_extent_multilevel_and_navigation(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_ext_path *path;
	struct ext4_extent ret_ex;
	ext4_lblk_t logical;
	ext4_fsblk_t phys;
	int ret;

	setup_depth1_two_leaf_tree(test, inode);

	/* Lookup first extent [10, 15) in leaf 0 */
	path = ext4_find_extent(inode, 10, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, path->p_depth, 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_next_allocated_block(path), 20U);
	KUNIT_EXPECT_EQ(test, ext4_ext_next_leaf_block_test(path), 40U);
	ext4_free_ext_path(path);

	/*
	 * Lookup lblk 30 (in hole between leaf 0 and leaf 1):
	 * path[1].p_ext points to [20, 25) (last extent in leaf 0).
	 */
	path = ext4_find_extent(inode, 30, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 20U);
	KUNIT_EXPECT_EQ(test, ext4_ext_next_allocated_block(path), 40U);

	/* Search left from lblk 30 finds last block of [20, 25) -> 24 @ 1104 */
	logical = 30;
	ret = ext4_ext_search_left_test(inode, path, &logical, &phys);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, logical, 24U);
	KUNIT_EXPECT_EQ(test, phys, 1104ULL);

	/* Search right from lblk 30 crosses into leaf 1 -> [40, 45) @ 2000 */
	logical = 30;
	ret = ext4_ext_search_right_test(inode, path, &logical, &phys, &ret_ex,
					 EXT4_EX_NOCACHE);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, logical, 40U);
	KUNIT_EXPECT_EQ(test, phys, 2000ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ret_ex.ee_block), 40U);
	ext4_free_ext_path(path);

	/* Lookup last extent [50, 55) in leaf 1 */
	path = ext4_find_extent(inode, 52, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 50U);
	KUNIT_EXPECT_EQ(test, ext4_ext_next_allocated_block(path),
			EXT_MAX_BLOCKS);
	KUNIT_EXPECT_EQ(test, ext4_ext_next_leaf_block_test(path),
			EXT_MAX_BLOCKS);
	ext4_free_ext_path(path);
}

/*
 * Verify ext4_ext_precache() walks all leaf blocks of a multi-level extent
 * tree, caches all extents and holes into the extent status tree, and sets
 * EXT4_STATE_EXT_PRECACHED.
 */
static void test_ext_precache(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct extent_status es;
	int err;

	setup_depth1_two_leaf_tree(test, inode);
	KUNIT_EXPECT_FALSE(test,
			   ext4_test_inode_state(inode,
						 EXT4_STATE_EXT_PRECACHED));

	err = ext4_ext_precache(inode);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test,
			  ext4_test_inode_state(inode,
						EXT4_STATE_EXT_PRECACHED));

	/* Verify cached inter-extent hole [15, 20) and extents from both leaves */
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 17, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_hole(&es));
	KUNIT_EXPECT_EQ(test, es.es_lblk, 15U);
	KUNIT_EXPECT_EQ(test, es.es_len, 5U);

	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 10, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_written(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 1000ULL);

	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 40, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_unwritten(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 2000ULL);

	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 50, NULL, &es, NULL),
			1);
	KUNIT_EXPECT_TRUE(test, ext4_es_is_written(&es));
	KUNIT_EXPECT_EQ(test, ext4_es_pblock(&es), 2100ULL);
}

/*
 * Verify ext4_can_extents_be_merged() rules for written/unwritten state,
 * logical and physical contiguity, and maximum length limits.
 */
static void test_can_extents_be_merged(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent ex1 = {}, ex2 = {};

	/* Contiguous written extents [10, 15) @ 100 and [15, 20) @ 105 */
	ex1.ee_block = cpu_to_le32(10);
	ex1.ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex1, 100);
	ex2.ee_block = cpu_to_le32(15);
	ex2.ee_len = cpu_to_le16(5);
	ext4_ext_store_pblock(&ex2, 105);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			1);

	/* Mismatched written vs unwritten cannot merge */
	ext4_ext_mark_unwritten(&ex2);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			0);

	/* Both unwritten and contiguous can merge */
	ext4_ext_mark_unwritten(&ex1);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			1);

	/* Non-contiguous physical blocks cannot merge */
	ext4_ext_mark_initialized(&ex1);
	ext4_ext_mark_initialized(&ex2);
	ext4_ext_store_pblock(&ex2, 200);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			0);

	/* Non-contiguous logical blocks cannot merge */
	ext4_ext_store_pblock(&ex2, 105);
	ex2.ee_block = cpu_to_le32(16);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			0);

	/* Written max length boundary (EXT_INIT_MAX_LEN = 32768) */
	ex1.ee_block = cpu_to_le32(0);
	ex1.ee_len = cpu_to_le16(32760);
	ext4_ext_store_pblock(&ex1, 100);
	ex2.ee_block = cpu_to_le32(32760);
	ex2.ee_len = cpu_to_le16(8);
	ext4_ext_store_pblock(&ex2, 100 + 32760);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			1);
	ex2.ee_len = cpu_to_le16(9);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			0);

	/* Unwritten max length boundary (EXT_UNWRITTEN_MAX_LEN = 32767) */
	ex1.ee_len = cpu_to_le16(32760);
	ext4_ext_mark_unwritten(&ex1);
	ex2.ee_len = cpu_to_le16(7);
	ext4_ext_mark_unwritten(&ex2);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			1);
	ex2.ee_len = cpu_to_le16(8);
	ext4_ext_mark_unwritten(&ex2);
	KUNIT_EXPECT_EQ(test, ext4_can_extents_be_merged_test(inode, &ex1, &ex2),
			0);
}

/*
 * Verify ext4_ext_insert_extent() appending, prepending, 3-way bridge merging,
 * and EXT4_GET_BLOCKS_SPLIT_NOMERGE suppression.
 */
static void test_insert_extent_merge_and_nomerge(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;

	ext_reset_tree(inode);

	/* Insert [10, 15) @ 100 and [20, 25) @ 110 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 5, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 20, 5, 110, false, 0), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);

	/* Append [15, 18) @ 105 -> merges with [10, 15) into [10, 18) */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 15, 3, 105, false, 0), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);
	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 8);

	/* Insert [18, 20) @ 108 -> bridges [10, 18) and [20, 25) into [10, 25) */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 18, 2, 108, false, 0), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 15);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[0]), 100ULL);

	/* Insert contiguous [25, 30) @ 115 with SPLIT_NOMERGE -> stays separate */
	KUNIT_ASSERT_EQ(test,
			ext_insert_one(inode, 25, 5, 115, false,
				       EXT4_GET_BLOCKS_SPLIT_NOMERGE),
			0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);
}

/*
 * Verify tree growth (depth 0 -> 1 via ext4_ext_grow_indepth), parent index
 * update when inserting a new lowest lblk (ext4_ext_correct_indexes), and
 * tree collapse back into the inode root (depth 1 -> 0 via
 * ext4_ext_try_to_merge_up) when merging reduces leaf entries to <= 4.
 */
static void test_insert_extent_grow_and_merge_up(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;

	ext_reset_tree(inode);

	/*
	 * Insert 4 non-contiguous extents that fill the root leaf (eh_max = 4):
	 * [10, 12) @ 100, [14, 16) @ 104, [18, 20) @ 108, [22, 24) @ 112.
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 2, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 14, 2, 104, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 18, 2, 108, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 22, 2, 112, false, 0), 0);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 4);

	/*
	 * Insert 5th extent [26, 28) @ 116: triggers ext4_ext_grow_indepth(),
	 * moving the leaf to a newly allocated external block and increasing
	 * tree depth to 1.
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 26, 2, 116, false, 0), 0);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)->ei_block), 10U);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 1U);

	/*
	 * Prepend [8, 10) @ 98 to [10, 12) @ 100: modifies first extent in the
	 * leaf and triggers ext4_ext_correct_indexes(), updating root ei_block
	 * from 10 to 8.
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 8, 2, 98, false, 0), 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)->ei_block), 8U);

	/*
	 * Bridge two gaps ([12, 14) @ 102 and [16, 18) @ 106): leaf entries
	 * shrink from 5 -> 4 -> 3. Once <= 4 (max_root), ext4_ext_try_to_merge_up()
	 * copies the extents back into the inode root and frees the leaf block!
	 */
	k_ctx.free_calls = 0;
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 12, 2, 102, false, 0), 0);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 4);
	KUNIT_EXPECT_EQ(test, k_ctx.free_calls, 1U);

	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 16, 2, 106, false, 0), 0);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);
	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 8U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 12);
}

/*
 * Verify ext4_ext_split() at depth 1 both when splitting a full leaf in the
 * middle (moving tail extents to a newly allocated leaf block) and when
 * appending past the end of a full leaf (m == 0).
 */
static void test_insert_extent_leaf_split(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_ext_path *path;
	int i;

	ext_reset_tree(inode);

	/*
	 * Insert 340 non-contiguous extents [i * 4, i * 4 + 2) @ 2000 + i * 4
	 * to grow from depth 0 to depth 1 and completely fill the external
	 * 4096-byte leaf block (eh_max = 340).
	 */
	for (i = 0; i < 340; i++) {
		KUNIT_ASSERT_EQ_MSG(test,
				    ext_insert_one(inode, i * 4, 2,
						   2000 + i * 4, false, 0),
				    0, "i=%d", i);
	}
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);

	/*
	 * Insert [402, 403) @ 9000 in the middle of the full leaf between
	 * [400, 402) and [404, 406): triggers ext4_ext_split() with m = 239,
	 * moving [404..1358) into a newly allocated second leaf block.
	 */
	k_ctx.alloc_calls = 0;
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 402, 1, 9000, false, 0), 0);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 1U);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)[0].ei_block), 0U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)[1].ei_block),
			404U);
	KUNIT_EXPECT_EQ(test, ext4_ext_check_inode(inode), 0);

	path = ext4_find_extent(inode, 402, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(path[1].p_hdr->eh_entries), 102);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 402U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[1].p_ext), 9000ULL);
	ext4_free_ext_path(path);

	path = ext4_find_extent(inode, 404, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(path[1].p_hdr->eh_entries), 239);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 404U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[1].p_ext), 2404ULL);
	ext4_free_ext_path(path);

	/*
	 * Fill the second leaf to capacity (239 + 101 = 340 entries), then
	 * append [1764, 1766) @ 3764 past its end to exercise ext4_ext_split()
	 * with m == 0 (adding a new leaf without moving existing entries).
	 */
	for (i = 340; i < 441; i++) {
		KUNIT_ASSERT_EQ_MSG(test,
				    ext_insert_one(inode, i * 4, 2,
						   2000 + i * 4, false, 0),
				    0, "i=%d", i);
	}
	k_ctx.alloc_calls = 0;
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 1764, 2, 3764, false, 0),
			0);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 1U);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)[2].ei_block),
			1764U);

	path = ext4_find_extent(inode, 1764, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, le16_to_cpu(path[1].p_hdr->eh_entries), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[1].p_ext->ee_block), 1764U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[1].p_ext), 3764ULL);
	ext4_free_ext_path(path);
}

/*
 * Verify multi-level tree growth from depth 1 -> 2 followed by leaf split
 * (ext4_ext_create_new_leaf repeat loop), and intermediate index block
 * splitting in ext4_ext_split() when both leaf and depth-1 index are full.
 */
static void test_insert_extent_index_split(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent_idx *idx;
	struct ext4_ext_path *path;
	struct buffer_head *bh;
	int i;

	ext_reset_tree(inode);

	/*
	 * Build a full depth-1 root index (eh_entries = eh_max = 4) pointing
	 * to 4 full leaf blocks (eh_entries = eh_max = 2).
	 */
	eh->eh_depth = cpu_to_le16(1);
	eh->eh_entries = cpu_to_le16(4);
	eh->eh_max = cpu_to_le16(ext4_ext_space_root_idx_test(inode, 0));
	idx = EXT_FIRST_INDEX(eh);

	for (i = 0; i < 4; i++) {
		struct ext4_extent_header *leaf_eh;
		struct ext4_extent *ex;
		ext4_lblk_t lblk = 10 + i * 10;

		idx[i].ei_block = cpu_to_le32(lblk);
		ext4_idx_store_pblock(&idx[i], EXT_META_BASE_PBLK + i);

		bh = ext4_ext_getblk_stub(inode, EXT_META_BASE_PBLK + i,
					  GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, bh);
		leaf_eh = ext_block_hdr(bh);
		leaf_eh->eh_magic = EXT4_EXT_MAGIC;
		leaf_eh->eh_depth = 0;
		leaf_eh->eh_entries = cpu_to_le16(2);
		leaf_eh->eh_max = cpu_to_le16(2);
		ex = EXT_FIRST_EXTENT(leaf_eh);
		ex[0].ee_block = cpu_to_le32(lblk);
		ex[0].ee_len = cpu_to_le16(2);
		ext4_ext_store_pblock(&ex[0], 2000 + i * 10);
		ex[1].ee_block = cpu_to_le32(lblk + 4);
		ex[1].ee_len = cpu_to_le16(2);
		ext4_ext_store_pblock(&ex[1], 2004 + i * 10);
		brelse(bh);
	}

	/*
	 * Inserting [48, 50) @ 2038 finds both leaf 3 and the root index full:
	 * ext4_ext_grow_indepth() grows depth 1 -> 2, and the repeat loop in
	 * ext4_ext_create_new_leaf() calls ext4_ext_split() at depth 1.
	 */
	k_ctx.alloc_calls = 0;
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 48, 2, 2038, false, 0), 0);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 2U);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 2);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);

	/*
	 * Clamp the depth-1 index block's eh_max to its current 5 entries so
	 * inserting [12, 13) @ 9000 into full leaf 0 splits both leaf 0 and
	 * the intermediate depth-1 index block (depth = 2, at = 0, k = 1).
	 */
	bh = ext4_ext_getblk_stub(inode, ext4_idx_pblock(EXT_FIRST_INDEX(eh)),
				  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bh);
	KUNIT_ASSERT_EQ(test, le16_to_cpu(ext_block_hdr(bh)->eh_entries), 5);
	ext_block_hdr(bh)->eh_max = cpu_to_le16(5);
	brelse(bh);

	k_ctx.alloc_calls = 0;
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 12, 1, 9000, false, 0), 0);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 2U);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 2);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)[0].ei_block),
			10U);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(EXT_FIRST_INDEX(eh)[1].ei_block),
			14U);
	KUNIT_EXPECT_EQ(test, ext4_ext_check_inode(inode), 0);

	path = ext4_find_extent(inode, 12, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, path->p_depth, 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[2].p_ext->ee_block), 12U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[2].p_ext), 9000ULL);
	ext4_free_ext_path(path);

	path = ext4_find_extent(inode, 14, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, path->p_depth, 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[2].p_ext->ee_block), 14U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[2].p_ext), 2004ULL);
	ext4_free_ext_path(path);

	path = ext4_find_extent(inode, 48, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	KUNIT_EXPECT_EQ(test, path->p_depth, 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(path[2].p_ext->ee_block), 48U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(path[2].p_ext), 2038ULL);
	ext4_free_ext_path(path);
}

/*
 * Verify ext4_ext_remove_space() for middle-of-extent hole punching, full
 * extent removal with left-scooting, and ext4_ext_truncate() tail removal.
 */
static void test_remove_space_tail_and_hole_punch(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	int err;

	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 10, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 30, 10, 200, false, 0), 0);

	/*
	 * Punch hole [12, 17] inside [10, 20): splits [10, 20) into [10, 12)
	 * and [18, 20), leaving 3 extents total.
	 */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_remove_space(inode, 12, 17);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[1].ee_block), 18U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[1]), 2);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[1]), 108ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[2].ee_block), 30U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[2]), 10);

	/*
	 * Punch out the entire middle extent [18, 19]: scoots [30, 40) into
	 * slot 1 and reduces eh_entries to 2.
	 */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_remove_space(inode, 18, 19);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[1].ee_block), 30U);

	/*
	 * Truncate file to 11 blocks (i_size = 11 * 4096): removes [30, 40)
	 * and trims [10, 12) down to [10, 11).
	 */
	inode->i_size = 11ULL << inode->i_sb->s_blocksize_bits;
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_truncate(NULL, inode);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 1);
}

/*
 * Verify ext4_ext_remove_space() on a depth-1 tree removes all leaf extents,
 * removes empty index entries via ext4_ext_rm_idx(), frees the leaf metadata
 * blocks, and resets the root header to eh_depth == 0.
 */
static void test_remove_space_multilevel_collapse(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	int err;

	setup_depth1_two_leaf_tree(test, inode);
	k_ctx.free_calls = 0;
	k_ctx.total_freed_blocks = 0;

	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_remove_space(inode, 0, EXT_MAX_BLOCKS - 1);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ext_depth(inode), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_max), 4);
	/* 4 data extents (5 blocks each = 20) + 2 leaf metadata blocks = 22 */
	KUNIT_EXPECT_EQ(test, k_ctx.total_freed_blocks, 22UL);
}

/*
 * Verify bigalloc partial cluster handling in ext4_ext_remove_space():
 * 1. When a cluster is shared with a remaining extent (state == nofree),
 *    the physical cluster is not freed.
 * 2. When the entire unaligned extent is removed and has a pending reservation
 *    in i_pending_tree, the cluster is freed with RERESERVE_CLUSTER and
 *    ext4_rereserve_cluster() re-increments i_reserved_data_blocks and removes
 *    the pending reservation.
 */
static void test_remove_space_bigalloc_partial_cluster(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	int err;

	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;

	/*
	 * Case 1: [0, 2) @ 100 and [2, 4) @ 102 share physical cluster 25
	 * (blocks 100..103). Punching [0, 1] must NOT free cluster 25.
	 */
	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test,
			ext_insert_one(inode, 0, 2, 100, false,
				       EXT4_GET_BLOCKS_SPLIT_NOMERGE),
			0);
	KUNIT_ASSERT_EQ(test,
			ext_insert_one(inode, 2, 2, 102, false,
				       EXT4_GET_BLOCKS_SPLIT_NOMERGE),
			0);

	k_ctx.free_calls = 0;
	k_ctx.total_freed_blocks = 0;
	down_write(&ei->i_data_sem);
	err = ext4_ext_remove_space(inode, 0, 1);
	up_write(&ei->i_data_sem);
	KUNIT_ASSERT_EQ(test, err, 0);
	/*
	 * ext4_remove_blocks() is called with NOFREE_FIRST_CLUSTER |
	 * NOFREE_LAST_CLUSTER, and partial cluster 25 is marked nofree.
	 */
	KUNIT_EXPECT_EQ(test, k_ctx.free_calls, 1U);
	KUNIT_EXPECT_TRUE(test,
			  (k_ctx.last_free_flags &
			   EXT4_FREE_BLOCKS_NOFREE_FIRST_CLUSTER) != 0);

	/*
	 * Case 2: Single unaligned extent [1, 3) @ 101 in cluster 0 (pclu 25)
	 * with a pending reservation on cluster 0. Truncating from 1 frees
	 * cluster 25 with RERESERVE_CLUSTER and calls ext4_rereserve_cluster().
	 */
	ext_reset_tree(inode);
	set_opt(inode->i_sb, DELALLOC);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 1, 2, 101, true, 0), 0);

	down_write(&ei->i_data_sem);
	/* Insert delayed extent at lblk 0 in already-allocated cluster 0 */
	ext4_es_insert_delayed_extent(inode, 0, 1, true, false);
	up_write(&ei->i_data_sem);
	KUNIT_ASSERT_TRUE(test, ext4_is_pending(inode, 0));

	ei->i_reserved_data_blocks = 0;
	k_ctx.free_calls = 0;
	down_write(&ei->i_data_sem);
	err = ext4_ext_remove_space(inode, 1, EXT_MAX_BLOCKS - 1);
	up_write(&ei->i_data_sem);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, k_ctx.last_free_pblk, 100ULL);
	KUNIT_EXPECT_EQ(test, k_ctx.last_free_count, 4UL);
	KUNIT_EXPECT_TRUE(test,
			  (k_ctx.last_free_flags &
			   EXT4_FREE_BLOCKS_RERESERVE_CLUSTER) != 0);
	KUNIT_EXPECT_EQ(test, ei->i_reserved_data_blocks, 1U);
	KUNIT_EXPECT_FALSE(test, ext4_is_pending(inode, 0));
}

/*
 * Verify ext4_ext_map_blocks() hole lookup, delayed-extent hole truncation
 * (ext4_ext_determine_insert_hole), and EXT4_GET_BLOCKS_QUERY_LAST_IN_LEAF.
 */
static void test_map_blocks_lookup_hole_and_query_last_in_leaf(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_map_blocks map = {};
	int ret;

	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 5, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 20, 5, 200, false, 0), 0);

	/* Hole lookup at [2, 22) returns 0 and trims m_len to [2, 10) (len 8) */
	map.m_lblk = 2;
	map.m_len = 20;
	map.m_flags = 0;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, 0);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 0ULL);
	KUNIT_EXPECT_EQ(test, map.m_len, 8U);

	/*
	 * Insert a delayed extent at [5, 7) in the es tree: hole lookup at
	 * [2, 22) must stop at the delayed extent -> [2, 5) (len 3).
	 */
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	ext4_es_insert_delayed_extent(inode, 5, 2, false, false);
	up_write(&EXT4_I(inode)->i_data_sem);

	map.m_lblk = 2;
	map.m_len = 20;
	map.m_flags = 0;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_EX_NOCACHE);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, map.m_len, 3U);

	/* QUERY_LAST_IN_LEAF on first extent [10, 15) -> flag NOT set */
	map.m_lblk = 10;
	map.m_len = 5;
	map.m_flags = 0;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map,
				  EXT4_GET_BLOCKS_QUERY_LAST_IN_LEAF);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_FALSE(test,
			   (map.m_flags & EXT4_MAP_QUERY_LAST_IN_LEAF) != 0);

	/* QUERY_LAST_IN_LEAF on last extent [20, 25) -> flag IS set */
	map.m_lblk = 20;
	map.m_len = 5;
	map.m_flags = 0;
	down_read(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map,
				  EXT4_GET_BLOCKS_QUERY_LAST_IN_LEAF);
	up_read(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_TRUE(test,
			  (map.m_flags & EXT4_MAP_QUERY_LAST_IN_LEAF) != 0);
}

/*
 * Verify ext4_ext_map_blocks() block allocation with overlap clamping
 * (ext4_ext_check_overlap) and freeing of newly allocated blocks when
 * ext4_ext_insert_extent() fails with -ENOSPC.
 */
static void test_map_blocks_alloc_overlap_and_enospc(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_map_blocks map = {};
	int ret;

	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 15, 5, 200, false, 0), 0);

	/*
	 * Request allocation for [10, 20) (len 10): ext4_ext_check_overlap()
	 * clamps the allocation to [10, 15) (len 5) to avoid overlapping [15, 20).
	 */
	k_ctx.alloc_calls = 0;
	map.m_lblk = 10;
	map.m_len = 10;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test, ret, 5);
	KUNIT_EXPECT_EQ(test, map.m_len, 5U);
	KUNIT_EXPECT_EQ(test, map.m_pblk, EXT_ALLOC_BASE_PBLK);
	KUNIT_EXPECT_TRUE(test,
			  (map.m_flags & (EXT4_MAP_NEW | EXT4_MAP_MAPPED)) ==
				  (EXT4_MAP_NEW | EXT4_MAP_MAPPED));
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 1U);
	KUNIT_EXPECT_EQ(test, k_ctx.last_alloc_len, 5U);

	/*
	 * Force ext4_ext_insert_extent() to return -ENOSPC and verify that
	 * ext4_ext_map_blocks() frees the newly allocated blocks.
	 */
	kunit_activate_static_stub(test, ext4_ext_insert_extent,
				   ext4_ext_insert_extent_stub);
	k_ctx.free_calls = 0;
	map.m_lblk = 30;
	map.m_len = 4;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map,
				  EXT4_GET_BLOCKS_CREATE |
					  EXT4_GET_BLOCKS_DELALLOC_RESERVE);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
	KUNIT_EXPECT_EQ(test, k_ctx.free_calls, 1U);
	KUNIT_EXPECT_EQ(test, k_ctx.last_free_pblk, k_ctx.last_alloc_pblk);
	KUNIT_EXPECT_EQ(test, k_ctx.last_free_count, 4UL);
	KUNIT_EXPECT_EQ(test, k_ctx.last_free_flags,
			EXT4_FREE_BLOCKS_NO_QUOT_UPDATE);
}

/*
 * Verify ext4_ext_convert_to_initialized() fastpath block transfers to
 * abutting left (ex - 1) and right (ex + 1) initialized neighbors without
 * splitting into new extent entries.
 */
static void test_convert_to_initialized_abut_neighbors(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	struct ext4_map_blocks map = {};
	int ret;

	EXT4_SB(inode->i_sb)->s_extent_max_zeroout_kb = 0;
	ext_reset_tree(inode);

	/*
	 * Setup: [10, 12) @ 100 (written), [12, 16) @ 102 (unwritten),
	 * [16, 18) @ 106 (written).
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 2, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 12, 4, 102, true, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 16, 2, 106, false, 0), 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	/*
	 * Convert head of unwritten extent [12, 14): transfers 2 blocks to
	 * left neighbor [10, 12) -> [10, 14) written, [14, 16) unwritten.
	 */
	map.m_lblk = 12;
	map.m_len = 2;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 4);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex[0]), 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[1].ee_block), 14U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[1]), 2);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex[1]), 1);

	/*
	 * Convert tail of unwritten extent [15, 16): transfers 1 block to
	 * right neighbor [16, 18) -> [14, 15) unwritten, [15, 18) written.
	 * Drop stale unwritten extent status entries cached during the first
	 * ext4_ext_map_blocks() call (normally updated by ext4_map_blocks()).
	 */
	map.m_lblk = 15;
	map.m_len = 1;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[1].ee_block), 14U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[1]), 1);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex[1]), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[2].ee_block), 15U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[2]), 3);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex[2]), 105ULL);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex[2]), 0);
}

/*
 * Verify bigalloc get_implied_cluster_alloc() in ext4_ext_map_blocks() when
 * an existing extent to the left or right already maps the same logical
 * cluster, avoiding a duplicate physical cluster allocation.
 */
static void test_map_blocks_bigalloc_implied_cluster(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_map_blocks map = {};
	int ret;

	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;

	/*
	 * Case 1: Left extent [8, 9) @ 100 in logical cluster 2 (lblks 8..11).
	 * Allocating [10, 12) reuses physical cluster 25 (pblks 102..103)
	 * without calling ext4_mb_new_blocks().
	 */
	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 8, 1, 100, false, 0), 0);

	k_ctx.alloc_calls = 0;
	map.m_lblk = 10;
	map.m_len = 2;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 102ULL);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 0U);

	/*
	 * Case 2: Right extent [10, 12) @ 102 in logical cluster 2 (lblks 8..11).
	 * Allocating [8, 10) searches right, finds [10, 12), and reuses
	 * physical cluster 25 (pblks 100..101) without calling ext4_mb_new_blocks().
	 */
	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 2, 102, false, 0), 0);

	k_ctx.alloc_calls = 0;
	map.m_lblk = 8;
	map.m_len = 2;
	map.m_flags = 0;
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_map_blocks(NULL, inode, &map, EXT4_GET_BLOCKS_CREATE);
	up_write(&EXT4_I(inode)->i_data_sem);

	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, map.m_pblk, 100ULL);
	KUNIT_EXPECT_EQ(test, k_ctx.alloc_calls, 0U);
}

/*
 * Verify ext4_ext_shift_extents() for SHIFT_LEFT (collapse range) with
 * neighbor merging, SHIFT_RIGHT (insert range), and -EINVAL boundary checks.
 */
static void test_ext_shift_extents(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	int err;

	ext_reset_tree(inode);
	/* [10, 15) @ 100 and [20, 25) @ 105 (physically contiguous!) */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 5, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 20, 5, 105, false, 0), 0);

	/* Shifting [20, 25) left by 6 overlaps [10, 15) -> -EINVAL */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_shift_extents_test(inode, NULL, 20, 6, SHIFT_LEFT);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* Shifting [20, 25) left by 5 merges with [10, 15) into [10, 20) @ 100 */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_shift_extents_test(inode, NULL, 20, 5, SHIFT_LEFT);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);
	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 10);

	/* Shifting [10, 20) right by 4 moves it to [14, 24) @ 100 */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_shift_extents_test(inode, NULL, 10, 4, SHIFT_RIGHT);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 14U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 10);

	/* Shifting right past EXT_MAX_BLOCKS -> -EINVAL */
	down_write(&EXT4_I(inode)->i_data_sem);
	err = ext4_ext_shift_extents_test(inode, NULL, 14, EXT_MAX_BLOCKS,
					  SHIFT_RIGHT);
	up_write(&EXT4_I(inode)->i_data_sem);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);
}

/*
 * Verify ext4_swap_extents() splits unaligned boundaries on both inodes,
 * swaps physical block mappings for the requested subrange, and marks
 * the donor inode's swapped extent unwritten when requested.
 */
static void test_swap_extents(struct kunit *test)
{
	struct inode *inode1 = &k_ctx.k_ei->vfs_inode;
	struct inode *inode2;
	struct ext4_extent_header *eh1, *eh2;
	struct ext4_extent *ex1, *ex2;
	int replaced, err = 0;

	k_ctx.k_ei2 = kzalloc_obj(struct ext4_inode_info);
	KUNIT_ASSERT_NOT_NULL(test, k_ctx.k_ei2);
	ext_init_mock_inode(k_ctx.k_ei2, inode1->i_sb, 13);
	inode2 = &k_ctx.k_ei2->vfs_inode;

	ext_reset_tree(inode1);
	ext_reset_tree(inode2);

	/* inode1: [10, 20) @ 100; inode2: [10, 20) @ 200 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode1, 10, 10, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode2, 10, 10, 200, false, 0), 0);

	lock_two_nondirectories(inode1, inode2);
	ext4_double_down_write_data_sem(inode1, inode2);

	replaced = ext4_swap_extents(NULL, inode1, inode2, 12, 12, 5, 1, &err);

	ext4_double_up_write_data_sem(inode1, inode2);
	unlock_two_nondirectories(inode1, inode2);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, replaced, 5);

	eh1 = ext_inode_hdr(inode1);
	eh2 = ext_inode_hdr(inode2);
	KUNIT_ASSERT_EQ(test, le16_to_cpu(eh1->eh_entries), 3);
	KUNIT_ASSERT_EQ(test, le16_to_cpu(eh2->eh_entries), 3);

	ex1 = EXT_FIRST_EXTENT(eh1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex1[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex1[0]), 100ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex1[1].ee_block), 12U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex1[1]), 5);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex1[1]), 202ULL);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex1[1]), 0);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex1[2].ee_block), 17U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex1[2]), 107ULL);

	ex2 = EXT_FIRST_EXTENT(eh2);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex2[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex2[0]), 200ULL);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex2[1].ee_block), 12U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex2[1]), 5);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex2[1]), 102ULL);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex2[1]), 1);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex2[2].ee_block), 17U);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex2[2]), 207ULL);
}

/*
 * Verify ext4_clu_mapped() across inline-data inodes, empty trees, clusters
 * mapped at the start or end of a cluster, and unmapped clusters.
 */
static void test_clu_mapped(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	sbi->s_cluster_bits = 2;
	sbi->s_cluster_ratio = 4;

	ext_reset_tree(inode);
	/* Empty tree -> unmapped */
	KUNIT_EXPECT_EQ(test, ext4_clu_mapped(inode, 0), 0);

	/*
	 * Insert [0, 2) @ 100 (in lclu 0 = lblks 0..3) and
	 * [6, 8) @ 106 (in lclu 1 = lblks 4..7).
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 2, 100, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 6, 2, 106, false, 0), 0);

	/* Inline-data state returns 0 immediately */
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	KUNIT_EXPECT_EQ(test, ext4_clu_mapped(inode, 0), 0);
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);

	/* lclu 0 (lblk 0) is mapped directly by [0, 2) */
	KUNIT_EXPECT_EQ(test, ext4_clu_mapped(inode, 0), 1);
	/* lclu 1 (lblk 4) is mapped by next allocated extent [6, 8) */
	KUNIT_EXPECT_EQ(test, ext4_clu_mapped(inode, 1), 1);
	/* lclu 2 (lblk 8..11) is unmapped */
	KUNIT_EXPECT_EQ(test, ext4_clu_mapped(inode, 2), 0);
}

/*
 * Verify ext4_ext_index_trans_blocks() and
 * ext4_ext_calc_credits_for_single_extent() credit calculations.
 */
static void test_ext_credits_calculation(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_ext_path *path;
	int credits_with_space, credits_full;

	/* 1 extent vs multiple extents */
	KUNIT_EXPECT_EQ(test, ext4_ext_index_trans_blocks(inode, 1),
			EXT4_MAX_EXTENT_DEPTH * 2 + 1);
	KUNIT_EXPECT_EQ(test, ext4_ext_index_trans_blocks(inode, 2),
			EXT4_MAX_EXTENT_DEPTH * 3 + 2);

	/* Inline data inode needs only 1 index block credit */
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	EXT4_I(inode)->i_inline_off = 1;
	KUNIT_EXPECT_EQ(test, ext4_ext_index_trans_blocks(inode, 2), 1);
	EXT4_I(inode)->i_inline_off = 0;
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);

	ext_reset_tree(inode);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 2, 100, false, 0), 0);

	path = ext4_find_extent(inode, 10, NULL, EXT4_EX_NOCACHE);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(path));
	/* Leaf has free space (eh_entries = 1 < eh_max = 4) */
	credits_with_space =
		ext4_ext_calc_credits_for_single_extent(inode, 1, path);

	/* Mark leaf full (eh_entries == eh_max) */
	eh->eh_entries = eh->eh_max;
	credits_full = ext4_ext_calc_credits_for_single_extent(inode, 1, path);
	eh->eh_entries = cpu_to_le16(1);
	ext4_free_ext_path(path);

	KUNIT_EXPECT_LT(test, credits_with_space, credits_full);
	KUNIT_EXPECT_EQ(test, credits_full,
			ext4_ext_calc_credits_for_single_extent(inode, 1, NULL));
}

/*
 * Verify fast-commit replay extent helpers:
 * - ext4_ext_replay_update_ex() splits and updates extent state/pblk
 * - ext4_ext_replay_shrink_inode() merges adjacent compatible extents
 * - ext4_ext_replay_set_iblocks() counts data and extent tree blocks and
 *   updates inode->i_blocks.
 */
static void test_ext_fast_commit_replay(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_extent_header *eh = ext_inode_hdr(inode);
	struct ext4_extent *ex;
	unsigned short saved_mount_state;
	int err;

	ext_reset_tree(inode);
	saved_mount_state = sbi->s_mount_state;
	sbi->s_mount_state |= EXT4_FC_REPLAY;

	/* Start with unwritten extent [10, 20) @ 100 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 10, 10, 100, true, 0), 0);

	/* Replay update converting middle [12, 16) to written @ 102 */
	err = ext4_ext_replay_update_ex(inode, 12, 4, 0, 102);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	/* Replay update converting [10, 12) @ 100 and [16, 20) @ 106 to written */
	KUNIT_ASSERT_EQ(test, ext4_ext_replay_update_ex(inode, 10, 2, 0, 100),
			0);
	KUNIT_ASSERT_EQ(test, ext4_ext_replay_update_ex(inode, 16, 4, 0, 106),
			0);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 3);

	/* Shrink inode merges the 3 contiguous written extents into [10, 20) */
	ext4_ext_replay_shrink_inode(inode, 25);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(eh->eh_entries), 1);
	ex = EXT_FIRST_EXTENT(eh);
	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex[0].ee_block), 10U);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex[0]), 10);
	KUNIT_EXPECT_EQ(test, ext4_ext_is_unwritten(&ex[0]), 0);

	/* Recalculate i_blocks for the 10-block extent (10 * 8 = 80 sectors) */
	inode->i_size = 30ULL << inode->i_sb->s_blocksize_bits;
	inode->i_blocks = 0;
	err = ext4_ext_replay_set_iblocks(inode);
	sbi->s_mount_state = saved_mount_state;
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, inode->i_blocks,
			10ULL << (inode->i_sb->s_blocksize_bits - 9));
}

static int ext_range_ext_zeroout_stub(handle_t *handle, struct inode *inode,
				      struct ext4_extent *ex)
{
	k_ctx.zeroout_calls++;
	k_ctx.last_zeroout_lblk = le32_to_cpu(ex->ee_block);
	k_ctx.last_zeroout_len = ext4_ext_get_actual_len(ex);
	return 0;
}

static int ext_range_issue_zeroout_stub(handle_t *handle, struct inode *inode,
					ext4_lblk_t lblk, ext4_fsblk_t pblk,
					ext4_lblk_t len)
{
	k_ctx.zeroout_calls++;
	k_ctx.last_zeroout_lblk = lblk;
	k_ctx.last_zeroout_len = len;
	return 0;
}

static int ext_orphan_add_stub(handle_t *handle, struct inode *inode)
{
	k_ctx.orphan_add_calls++;
	return 0;
}

static int ext_orphan_del_stub(handle_t *handle, struct inode *inode)
{
	k_ctx.orphan_del_calls++;
	return 0;
}

static void ext_setup_range_test(struct kunit *test, struct inode *inode,
				 struct file *file)
{
	kunit_activate_static_stub(test, ext4_ext_zeroout,
				   ext_range_ext_zeroout_stub);
	kunit_activate_static_stub(test, ext4_issue_zeroout,
				   ext_range_issue_zeroout_stub);
	kunit_activate_static_stub(test, ext4_orphan_add, ext_orphan_add_stub);
	kunit_activate_static_stub(test, ext4_orphan_del, ext_orphan_del_stub);

	ext_reset_tree(inode);
	inode->i_size = 0;
	EXT4_I(inode)->i_disksize = 0;
	inode->i_blocks = 0;

	k_ctx.next_alloc_pblk = EXT_ALLOC_BASE_PBLK;
	k_ctx.alloc_calls = 0;
	k_ctx.free_calls = 0;
	k_ctx.total_freed_blocks = 0;
	k_ctx.zeroout_calls = 0;
	k_ctx.orphan_add_calls = 0;
	k_ctx.orphan_del_calls = 0;

	if (file) {
		struct file tmp = {
			.f_path		= { .dentry = &k_ctx.k_dentry },
			.f_inode	= inode,
			.f_mapping	= inode->i_mapping,
			.f_mode		= FMODE_READ | FMODE_WRITE |
					  FMODE_NOCMTIME,
		};

		memset(&k_ctx.k_dentry, 0, sizeof(k_ctx.k_dentry));
		k_ctx.k_dentry.d_inode = inode;
		memcpy(file, &tmp, sizeof(tmp));
	}
}

static void ext_expect_extent_at(struct kunit *test, struct inode *inode,
				 ext4_lblk_t lblk, unsigned short exp_len,
				 ext4_fsblk_t exp_pblk, bool exp_unwritten)
{
	struct ext4_ext_path *path;
	struct ext4_extent ex;
	bool found = false;
	int err = 0;

	down_read(&EXT4_I(inode)->i_data_sem);
	path = ext4_find_extent(inode, lblk, NULL, EXT4_EX_NOCACHE);
	if (IS_ERR(path)) {
		err = PTR_ERR(path);
	} else {
		if (path[path->p_depth].p_ext) {
			ex = *path[path->p_depth].p_ext;
			found = true;
		}
		ext4_free_ext_path(path);
	}
	up_read(&EXT4_I(inode)->i_data_sem);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_TRUE(test, found);

	KUNIT_EXPECT_EQ(test, le32_to_cpu(ex.ee_block), lblk);
	KUNIT_EXPECT_EQ(test, ext4_ext_get_actual_len(&ex), exp_len);
	KUNIT_EXPECT_EQ(test, ext4_ext_pblock(&ex), exp_pblk);
	KUNIT_EXPECT_EQ(test, !!ext4_ext_is_unwritten(&ex), exp_unwritten);
}

/*
 * Verify ext4_fallocate() mode validation (unsupported flags, encrypted
 * collapse/insert rejection, WRITE_ZEROES without bdev support, non-extent
 * preallocation rejection), preallocation with and without FALLOC_FL_KEEP_SIZE,
 * and hole punching via FALLOC_FL_PUNCH_HOLE.
 */
static void test_fallocate_modes_and_prealloc(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct file file;
	long ret;

	ext_setup_range_test(test, inode, &file);

	/* Unsupported mode flag -> -EOPNOTSUPP */
	ret = ext4_fallocate(&file, 0x40000000, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);

	/* FALLOC_FL_WRITE_ZEROES without unmap sectors -> -EOPNOTSUPP */
	ret = ext4_fallocate(&file, FALLOC_FL_WRITE_ZEROES, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);

	/* Encrypted inode rejects COLLAPSE_RANGE and INSERT_RANGE */
	inode->i_flags |= S_ENCRYPTED;
	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	inode->i_flags &= ~S_ENCRYPTED;

	/* Non-extent (indirect) inode rejects fallocate preallocation */
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ret = ext4_fallocate(&file, 0, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);

	/* Preallocate [0, 4 blocks) with FALLOC_FL_KEEP_SIZE: i_size stays 0 */
	ret = ext4_fallocate(&file, FALLOC_FL_KEEP_SIZE, 0, 4 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 0LL);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 0LL);
	ext_expect_extent_at(test, inode, 0, 4, EXT_ALLOC_BASE_PBLK, true);

	/* Extend allocation across [2, 6 blocks) without KEEP_SIZE */
	ret = ext4_fallocate(&file, 0, 2 * 4096, 4 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 6LL * 4096);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 6LL * 4096);
	ext_expect_extent_at(test, inode, 0, 6, EXT_ALLOC_BASE_PBLK, true);

	/* Punch middle [2, 4) with FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE */
	ret = ext4_fallocate(&file, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			     2 * 4096, 2 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 6LL * 4096);
	KUNIT_EXPECT_EQ(test, k_ctx.total_freed_blocks, 2UL);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(inode)->eh_entries), 2);
	ext_expect_extent_at(test, inode, 0, 2, EXT_ALLOC_BASE_PBLK, true);
	ext_expect_extent_at(test, inode, 4, 2, EXT_ALLOC_BASE_PBLK + 4, true);
}

/*
 * Verify ext4_zero_range() via both FALLOC_FL_ZERO_RANGE (splitting written
 * extents to unwritten and allocating unwritten blocks past EOF) and
 * FALLOC_FL_WRITE_ZEROES (issuing block zeroouts, converting to written, and
 * tracking orphan state when extending i_disksize).
 */
static void test_zero_range_and_write_zeroes(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct super_block *sb = inode->i_sb;
	struct file file;
	long ret;

	/* Part A: FALLOC_FL_ZERO_RANGE across written tail + past EOF */
	ext_setup_range_test(test, inode, &file);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 6, 200, false, 0), 0);
	inode->i_size = 6LL * 4096;
	EXT4_I(inode)->i_disksize = 6LL * 4096;

	ret = ext4_fallocate(&file, FALLOC_FL_ZERO_RANGE, 2 * 4096, 6 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 8LL * 4096);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 8LL * 4096);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(inode)->eh_entries), 3);
	ext_expect_extent_at(test, inode, 0, 2, 200, false);
	ext_expect_extent_at(test, inode, 2, 4, 202, true);
	ext_expect_extent_at(test, inode, 6, 2, EXT_ALLOC_BASE_PBLK, true);

	/* Part B: FALLOC_FL_WRITE_ZEROES over unwritten + hole past EOF */
	ext_setup_range_test(test, inode, &file);
	sb->s_bdev->bd_queue->limits.max_wzeroes_unmap_sectors = 1024;

	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 2, 300, true, 0), 0);
	inode->i_size = 2LL * 4096;
	EXT4_I(inode)->i_disksize = 2LL * 4096;

	ret = ext4_fallocate(&file, FALLOC_FL_WRITE_ZEROES, 0, 4 * 4096);
	sb->s_bdev->bd_queue->limits.max_wzeroes_unmap_sectors = 0;

	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_GE(test, k_ctx.zeroout_calls, 2U);
	KUNIT_EXPECT_GE(test, k_ctx.orphan_add_calls, 1U);
	KUNIT_EXPECT_EQ(test, k_ctx.orphan_del_calls, 1U);
	KUNIT_EXPECT_EQ(test, inode->i_size, 4LL * 4096);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 4LL * 4096);
	ext_expect_extent_at(test, inode, 0, 2, 300, false);
	ext_expect_extent_at(test, inode, 2, 2, EXT_ALLOC_BASE_PBLK, false);
}

/*
 * Verify ext4_collapse_range() validation checks, block removal, left-shift of
 * subsequent extents, adjacent extent coalescing after collapse, and i_size
 * reduction.
 */
static void test_collapse_range(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct file file;
	long ret;

	ext_setup_range_test(test, inode, &file);
	inode->i_size = 10LL * 4096;
	EXT4_I(inode)->i_disksize = 10LL * 4096;

	/* Block-unaligned offset or length -> -EINVAL */
	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE, 512, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE, 4096, 512);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Non-extent inode -> -EOPNOTSUPP */
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);

	/* Collapsing up to or beyond i_size -> -EINVAL */
	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE,
			     8 * 4096, 2 * 4096);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/*
	 * Build [0, 2)@200 writ, [2, 4)@500 writ, [4, 7)@202 writ, and
	 * [8, 10)@600 unwrit. Collapsing [2, 4) frees [2, 4)@500, shifts
	 * [4, 7)@202 left to [2, 5)@202 where it merges with [0, 2)@200 into
	 * [0, 5)@200, and shifts [8, 10)@600 left to [6, 8)@600.
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 2, 200, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 2, 2, 500, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 4, 3, 202, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 8, 2, 600, true, 0), 0);

	ret = ext4_fallocate(&file, FALLOC_FL_COLLAPSE_RANGE,
			     2 * 4096, 2 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, k_ctx.total_freed_blocks, 2UL);
	KUNIT_EXPECT_EQ(test, inode->i_size, 8LL * 4096);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 8LL * 4096);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(inode)->eh_entries), 2);
	ext_expect_extent_at(test, inode, 0, 5, 200, false);
	ext_expect_extent_at(test, inode, 6, 2, 600, true);
}

/*
 * Verify ext4_insert_range() alignment, EOF, and s_maxbytes checks, as well as
 * mid-extent splitting, right-shift of trailing extents, and i_size growth.
 */
static void test_insert_range(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct file file;
	long ret;

	ext_setup_range_test(test, inode, &file);
	inode->i_size = 10LL * 4096;
	EXT4_I(inode)->i_disksize = 10LL * 4096;

	/* Block-unaligned offset or length -> -EINVAL */
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 1024, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 4096, 1024);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Non-extent inode -> -EOPNOTSUPP */
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 0, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);

	/* Offset at or beyond i_size -> -EINVAL */
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 10 * 4096, 4096);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Length exceeding s_maxbytes - i_size -> -EFBIG */
	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE, 4096,
			     round_down(inode->i_sb->s_maxbytes, 4096));
	KUNIT_EXPECT_EQ(test, ret, -EFBIG);

	/*
	 * Insert 3-block hole at lblk 2 inside [0, 6)@200 writ with trailing
	 * [8, 10)@400 unwrit: produces [0, 2)@200 writ, [5, 9)@202 writ, and
	 * [11, 13)@400 unwrit, with i_size = 13 blocks.
	 */
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 6, 200, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 8, 2, 400, true, 0), 0);

	ret = ext4_fallocate(&file, FALLOC_FL_INSERT_RANGE,
			     2 * 4096, 3 * 4096);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, inode->i_size, 13LL * 4096);
	KUNIT_EXPECT_EQ(test, EXT4_I(inode)->i_disksize, 13LL * 4096);
	KUNIT_EXPECT_EQ(test, le16_to_cpu(ext_inode_hdr(inode)->eh_entries), 3);
	ext_expect_extent_at(test, inode, 0, 2, 200, false);
	ext_expect_extent_at(test, inode, 5, 4, 202, false);
	ext_expect_extent_at(test, inode, 11, 2, 400, true);
}

/*
 * Verify unwritten-to-written IO completion conversion helpers:
 * ext4_convert_unwritten_extents(), ext4_convert_unwritten_extents_atomic()
 * across a split unwritten mapping, and ext4_convert_unwritten_io_end_vec().
 */
static void test_convert_unwritten_extents_and_io_end(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	ext4_io_end_t io_end = {};
	struct ext4_io_end_vec vec1 = {}, vec2 = {};
	ext4_lblk_t converted = 0;
	int err;

	/* 1. ext4_convert_unwritten_extents() on middle of unwritten extent */
	ext_setup_range_test(test, inode, NULL);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 6, 200, true, 0), 0);

	err = ext4_convert_unwritten_extents(NULL, inode, 2 * 4096, 2 * 4096,
					     &converted);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, converted, 2U);
	ext_expect_extent_at(test, inode, 0, 2, 200, true);
	ext_expect_extent_at(test, inode, 2, 2, 202, false);
	ext_expect_extent_at(test, inode, 4, 2, 204, true);

	/* 2. ext4_convert_unwritten_extents_atomic() across split unwritten */
	ext_setup_range_test(test, inode, NULL);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 2, 200, true, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 2, 3, 300, true, 0), 0);

	err = ext4_convert_unwritten_extents_atomic(NULL, inode, 0, 5 * 4096);
	KUNIT_ASSERT_EQ(test, err, 0);
	ext_expect_extent_at(test, inode, 0, 2, 200, false);
	ext_expect_extent_at(test, inode, 2, 3, 300, false);

	/* 3. ext4_convert_unwritten_io_end_vec() across multiple vecs */
	ext_setup_range_test(test, inode, NULL);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 8, 200, true, 0), 0);

	io_end.inode = inode;
	INIT_LIST_HEAD(&io_end.list_vec);
	vec1.offset = 1 * 4096;
	vec1.size = 2 * 4096;
	vec2.offset = 5 * 4096;
	vec2.size = 2 * 4096;
	list_add_tail(&vec1.list, &io_end.list_vec);
	list_add_tail(&vec2.list, &io_end.list_vec);

	err = ext4_convert_unwritten_io_end_vec(NULL, &io_end);
	KUNIT_ASSERT_EQ(test, err, 0);
	ext_expect_extent_at(test, inode, 0, 1, 200, true);
	ext_expect_extent_at(test, inode, 1, 2, 201, false);
	ext_expect_extent_at(test, inode, 3, 2, 203, true);
	ext_expect_extent_at(test, inode, 5, 2, 205, false);
	ext_expect_extent_at(test, inode, 7, 1, 207, true);
}

/*
 * Verify ext4_fiemap() (range checks, FIEMAP_FLAG_CACHE precaching, data and
 * external xattr reporting) and ext4_get_es_cache() (flag checks, precaching,
 * and reporting mapped + delayed status extents).
 */
static void test_ext_fiemap_and_get_es_cache(struct kunit *test)
{
	struct inode *inode = &k_ctx.k_ei->vfs_inode;
	struct fiemap_extent_info fieinfo = {};
	struct extent_status es = {};
	int err;

	ext_setup_range_test(test, inode, NULL);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 3, 200, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 6, 2, 300, true, 0), 0);
	inode->i_size = 10LL * 4096;
	EXT4_I(inode)->i_disksize = 10LL * 4096;

	/* Zero-length FIEMAP -> -EINVAL */
	err = ext4_fiemap(inode, &fieinfo, 0, 0);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* Start past maxbytes on non-extent inode -> -EFBIG */
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	err = ext4_fiemap(inode, &fieinfo,
			  EXT4_SB(inode->i_sb)->s_bitmap_maxbytes + 4096, 4096);
	KUNIT_EXPECT_EQ(test, err, -EFBIG);
	ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);

	/* Count-only data FIEMAP with FIEMAP_FLAG_CACHE populates ES cache */
	memset(&fieinfo, 0, sizeof(fieinfo));
	fieinfo.fi_flags = FIEMAP_FLAG_CACHE;
	err = ext4_fiemap(inode, &fieinfo, 0, 10 * 4096);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_flags & FIEMAP_FLAG_CACHE, 0U);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_extents_mapped, 2U);
	KUNIT_EXPECT_EQ(test, ext4_es_lookup_extent(inode, 0, NULL, &es, NULL),
			1);

	/* FIEMAP_FLAG_XATTR with no xattrs vs external i_file_acl block */
	memset(&fieinfo, 0, sizeof(fieinfo));
	fieinfo.fi_flags = FIEMAP_FLAG_XATTR;
	err = ext4_fiemap(inode, &fieinfo, 0, 4096);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_extents_mapped, 0U);

	EXT4_I(inode)->i_file_acl = 750;
	memset(&fieinfo, 0, sizeof(fieinfo));
	fieinfo.fi_flags = FIEMAP_FLAG_XATTR;
	err = ext4_fiemap(inode, &fieinfo, 0, 4096);
	EXT4_I(inode)->i_file_acl = 0;
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_extents_mapped, 1U);

	/* ext4_get_es_cache: unsupported flag -> -EBADR, len == 0 -> -EINVAL */
	memset(&fieinfo, 0, sizeof(fieinfo));
	fieinfo.fi_flags = FIEMAP_FLAG_XATTR;
	err = ext4_get_es_cache(inode, &fieinfo, 0, 4096);
	KUNIT_EXPECT_EQ(test, err, -EBADR);

	memset(&fieinfo, 0, sizeof(fieinfo));
	err = ext4_get_es_cache(inode, &fieinfo, 0, 0);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/*
	 * Populate ES tree with written, hole, delayed, and unwritten entries
	 * and verify ext4_get_es_cache reports all 4 entries.
	 */
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	ext4_es_insert_extent(inode, 0, 3, 200, EXTENT_STATUS_WRITTEN, 0);
	ext4_es_insert_extent(inode, 3, 1, ~0, EXTENT_STATUS_HOLE, 0);
	ext4_es_insert_delayed_extent(inode, 4, 2, false, false);
	ext4_es_insert_extent(inode, 6, 2, 300, EXTENT_STATUS_UNWRITTEN, 0);
	up_write(&EXT4_I(inode)->i_data_sem);

	memset(&fieinfo, 0, sizeof(fieinfo));
	err = ext4_get_es_cache(inode, &fieinfo, 0, 10 * 4096);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_extents_mapped, 4U);

	/*
	 * Build a depth-1 tree (5 discontiguous extents), clear ES cache, and
	 * verify FIEMAP_FLAG_CACHE precaches external leaf extents + holes.
	 */
	ext_setup_range_test(test, inode, NULL);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 0, 1, 200, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 2, 1, 202, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 4, 1, 204, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 6, 1, 206, false, 0), 0);
	KUNIT_ASSERT_EQ(test, ext_insert_one(inode, 8, 1, 208, false, 0), 0);
	inode->i_size = 10LL * 4096;
	EXT4_I(inode)->i_disksize = 10LL * 4096;

	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	up_write(&EXT4_I(inode)->i_data_sem);

	memset(&fieinfo, 0, sizeof(fieinfo));
	fieinfo.fi_flags = FIEMAP_FLAG_CACHE;
	err = ext4_get_es_cache(inode, &fieinfo, 0, 10 * 4096);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, fieinfo.fi_flags & FIEMAP_FLAG_CACHE, 0U);
	KUNIT_EXPECT_GE(test, fieinfo.fi_extents_mapped, 5U);
}

static const struct kunit_ext_test_param test_split_convert_params[] = {
	/* unwrit to writ splits */
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half to writ",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 3,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 2,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1 + (EXT_DATA_LEN - 2),
			       .ex_len = 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },

	/* writ to unwrit splits */
	{ .desc = "split writ extent to 2 extents and convert 1st half unwrit",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split writ extent to 2 extents and convert 2nd half unwrit",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split writ extent to 3 extents and convert 2nd half to unwrit",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 3,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 2,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1 + (EXT_DATA_LEN - 2),
			       .ex_len = 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },

	/*
	 * ***** zeroout tests *****
	 */
	/* unwrit to writ splits */
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half writ (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 3,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X', .off_blk = 1, .len_blk = EXT_DATA_LEN - 2 },
			      { .exp_char = 0, .off_blk = EXT_DATA_LEN - 1, .len_blk = 1 } } },

	/* writ to unwrit splits */
	{ .desc = "split writ extent to 2 extents and convert 1st half unwrit (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split writ extent to 2 extents and convert 2nd half unwrit (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split writ extent to 3 extents and convert 2nd half unwrit (zeroout)",
	  .type = TEST_SPLIT_CONVERT,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 3,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 2 },
			      { .exp_char = 'X',
				.off_blk = EXT_DATA_LEN - 1,
				.len_blk = 1 } } },
};

/* Tests to trigger ext4_ext_map_blocks() -> convert_initialized_extent() */
static const struct kunit_ext_test_param test_convert_initialized_params[] = {
	/* writ to unwrit splits */
	{ .desc = "split writ extent to 2 extents and convert 1st half unwrit",
	  .type = TEST_CREATE_BLOCKS,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .is_unwrit_at_start = 0,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split writ extent to 2 extents and convert 2nd half unwrit",
	  .type = TEST_CREATE_BLOCKS,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .is_unwrit_at_start = 0,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split writ extent to 3 extents and convert 2nd half to unwrit",
	  .type = TEST_CREATE_BLOCKS,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .is_unwrit_at_start = 0,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 3,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 2,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1 + (EXT_DATA_LEN - 2),
			       .ex_len = 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },

	/* writ to unwrit splits (zeroout) */
	{ .desc = "split writ extent to 2 extents and convert 1st half unwrit (zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split writ extent to 2 extents and convert 2nd half unwrit (zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split writ extent to 3 extents and convert 2nd half unwrit (zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 0,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT_UNWRITTEN,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 3,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 2 },
			      { .exp_char = 'X',
				.off_blk = EXT_DATA_LEN - 1,
				.len_blk = 1 } } },
};

/* Tests to trigger ext4_ext_map_blocks() -> ext4_ext_handle_unwritten_exntents() */
static const struct kunit_ext_test_param test_handle_unwritten_params[] = {
	/* unwrit to writ splits via endio path */
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ (endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ (endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 2,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half to writ (endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 3,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 2,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1 + (EXT_DATA_LEN - 2),
			       .ex_len = 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },

	/* unwrit to writ splits via non-endio path */
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ (non endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 2,
	  .disable_zeroout = true,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ (non endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 2,
	  .disable_zeroout = true,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 1,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 0 },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half to writ (non endio)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 3,
	  .disable_zeroout = true,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = 1,
			       .is_unwrit = 1 },
			     { .ex_lblk = EXT_DATA_LBLK + 1,
			       .ex_len = EXT_DATA_LEN - 2,
			       .is_unwrit = 0 },
			     { .ex_lblk = EXT_DATA_LBLK + 1 + (EXT_DATA_LEN - 2),
			       .ex_len = 1,
			       .is_unwrit = 1 } },
	  .is_zeroout_test = 0 },

	/*
	 * ***** zeroout tests *****
	 */
	/* unwrit to writ splits (endio)*/
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ (endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ (endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half writ (endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CONVERT,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 3,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 2 },
			      { .exp_char = 0,
				.off_blk = EXT_DATA_LEN - 1,
				.len_blk = 1 } } },

	/* unwrit to writ splits (non-endio)*/
	{ .desc = "split unwrit extent to 2 extents and convert 1st half writ (non-endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK, .m_len = 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 'X', .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 0,
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 2 extents and convert 2nd half writ (non-endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 1 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 2,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 1 } } },
	{ .desc = "split unwrit extent to 3 extents and convert 2nd half writ (non-endio, zeroout)",
	  .type = TEST_CREATE_BLOCKS,
	  .is_unwrit_at_start = 1,
	  .split_flags = EXT4_GET_BLOCKS_CREATE,
	  .split_map = { .m_lblk = EXT_DATA_LBLK + 1, .m_len = EXT_DATA_LEN - 2 },
	  .nr_exp_ext = 1,
	  .exp_ext_state = { { .ex_lblk = EXT_DATA_LBLK,
			       .ex_len = EXT_DATA_LEN,
			       .is_unwrit = 0 } },
	  .is_zeroout_test = 1,
	  .nr_exp_data_segs = 3,
	  .exp_data_state = { { .exp_char = 0, .off_blk = 0, .len_blk = 1 },
			      { .exp_char = 'X',
				.off_blk = 1,
				.len_blk = EXT_DATA_LEN - 2 },
			      { .exp_char = 0,
				.off_blk = EXT_DATA_LEN - 1,
				.len_blk = 1 } } },
};

static void ext_get_desc(struct kunit *test, const void *p, char *desc)

{
	struct kunit_ext_test_param *param = (struct kunit_ext_test_param *)p;

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s %s\n", param->desc,
		 (param->type & TEST_CREATE_BLOCKS) ? "(highlevel)" : "");
}

static int test_split_convert_param_init(struct kunit *test)
{
	size_t arr_size = ARRAY_SIZE(test_split_convert_params);

	kunit_register_params_array(test, test_split_convert_params, arr_size,
				    ext_get_desc);
	return 0;
}

static int test_convert_initialized_param_init(struct kunit *test)
{
	size_t arr_size = ARRAY_SIZE(test_convert_initialized_params);

	kunit_register_params_array(test, test_convert_initialized_params,
				    arr_size, ext_get_desc);
	return 0;
}

static int test_handle_unwritten_init(struct kunit *test)
{
	size_t arr_size = ARRAY_SIZE(test_handle_unwritten_params);

	kunit_register_params_array(test, test_handle_unwritten_params,
				    arr_size, ext_get_desc);
	return 0;
}

/*
 * Note that we use KUNIT_CASE_PARAM_WITH_INIT() instead of the more compact
 * KUNIT_ARRAY_PARAM() because the later currently has a limitation causing the
 * output parsing to be prone to error. For more context:
 *
 * https://lore.kernel.org/linux-kselftest/aULJpTvJDw9ctUDe@li-dc0c254c-257c-11b2-a85c-98b6c1322444.ibm.com/
 */
static struct kunit_case extents_test_cases[] = {
	KUNIT_CASE_PARAM_WITH_INIT(test_split_convert, kunit_array_gen_params,
				   test_split_convert_param_init, NULL),
	KUNIT_CASE_PARAM_WITH_INIT(test_split_convert, kunit_array_gen_params,
				   test_convert_initialized_param_init, NULL),
	KUNIT_CASE_PARAM_WITH_INIT(test_split_convert, kunit_array_gen_params,
				   test_handle_unwritten_init, NULL),
	KUNIT_CASE(test_ext_pblock_48bit),
	KUNIT_CASE(test_ext_unwritten_len_encoding),
	KUNIT_CASE(test_ext_tree_init_and_check_inode),
	KUNIT_CASE_PARAM_WITH_INIT(test_ext_check_corruptions,
				   kunit_array_gen_params,
				   ext_check_corrupt_param_init, NULL),
	KUNIT_CASE(test_ext_block_csum_verify),
	KUNIT_CASE(test_find_extent_root_binsearch),
	KUNIT_CASE(test_find_extent_multilevel_and_navigation),
	KUNIT_CASE(test_ext_precache),
	KUNIT_CASE(test_can_extents_be_merged),
	KUNIT_CASE(test_insert_extent_merge_and_nomerge),
	KUNIT_CASE(test_insert_extent_grow_and_merge_up),
	KUNIT_CASE(test_insert_extent_leaf_split),
	KUNIT_CASE(test_insert_extent_index_split),
	KUNIT_CASE(test_remove_space_tail_and_hole_punch),
	KUNIT_CASE(test_remove_space_multilevel_collapse),
	KUNIT_CASE(test_remove_space_bigalloc_partial_cluster),
	KUNIT_CASE(test_map_blocks_lookup_hole_and_query_last_in_leaf),
	KUNIT_CASE(test_map_blocks_alloc_overlap_and_enospc),
	KUNIT_CASE(test_convert_to_initialized_abut_neighbors),
	KUNIT_CASE(test_map_blocks_bigalloc_implied_cluster),
	KUNIT_CASE(test_ext_shift_extents),
	KUNIT_CASE(test_swap_extents),
	KUNIT_CASE(test_clu_mapped),
	KUNIT_CASE(test_ext_credits_calculation),
	KUNIT_CASE(test_ext_fast_commit_replay),
	KUNIT_CASE(test_fallocate_modes_and_prealloc),
	KUNIT_CASE(test_zero_range_and_write_zeroes),
	KUNIT_CASE(test_collapse_range),
	KUNIT_CASE(test_insert_range),
	KUNIT_CASE(test_convert_unwritten_extents_and_io_end),
	KUNIT_CASE(test_ext_fiemap_and_get_es_cache),
	{}
};

static struct kunit_suite extents_test_suite = {
	.name = "ext4_extents_test",
	.init = extents_kunit_init,
	.exit = extents_kunit_exit,
	.test_cases = extents_test_cases,
};

kunit_test_suites(&extents_test_suite);

MODULE_LICENSE("GPL");
