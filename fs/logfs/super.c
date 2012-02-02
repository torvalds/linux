/*
 * fs/logfs/super.c
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 *
 * Generally contains mount/umount code and also serves as a dump area for
 * any functions that don't fit elsewhere and neither justify a file of their
 * own.
 */
#include "logfs.h"
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>

static DEFINE_MUTEX(emergency_mutex);
static struct page *emergency_page;

struct page *emergency_read_begin(struct address_space *mapping, pgoff_t index)
{
	filler_t *filler = (filler_t *)mapping->a_ops->readpage;
	struct page *page;
	int err;

	page = read_cache_page(mapping, index, filler, NULL);
	if (page)
		return page;

	/* No more pages available, switch to emergency page */
	printk(KERN_INFO"Logfs: Using emergency page\n");
	mutex_lock(&emergency_mutex);
	err = filler(NULL, emergency_page);
	if (err) {
		mutex_unlock(&emergency_mutex);
		printk(KERN_EMERG"Logfs: Error reading emergency page\n");
		return ERR_PTR(err);
	}
	return emergency_page;
}

void emergency_read_end(struct page *page)
{
	if (page == emergency_page)
		mutex_unlock(&emergency_mutex);
	else
		page_cache_release(page);
}

static void dump_segfile(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_segment_entry se;
	u32 segno;

	for (segno = 0; segno < super->s_no_segs; segno++) {
		logfs_get_segment_entry(sb, segno, &se);
		printk("%3x: %6x %8x", segno, be32_to_cpu(se.ec_level),
				be32_to_cpu(se.valid));
		if (++segno < super->s_no_segs) {
			logfs_get_segment_entry(sb, segno, &se);
			printk(" %6x %8x", be32_to_cpu(se.ec_level),
					be32_to_cpu(se.valid));
		}
		if (++segno < super->s_no_segs) {
			logfs_get_segment_entry(sb, segno, &se);
			printk(" %6x %8x", be32_to_cpu(se.ec_level),
					be32_to_cpu(se.valid));
		}
		if (++segno < super->s_no_segs) {
			logfs_get_segment_entry(sb, segno, &se);
			printk(" %6x %8x", be32_to_cpu(se.ec_level),
					be32_to_cpu(se.valid));
		}
		printk("\n");
	}
}

/*
 * logfs_crash_dump - dump debug information to device
 *
 * The LogFS superblock only occupies part of a segment.  This function will
 * write as much debug information as it can gather into the spare space.
 */
void logfs_crash_dump(struct super_block *sb)
{
	dump_segfile(sb);
}

/*
 * FIXME: There should be a reserve for root, similar to ext2.
 */
int logfs_statfs(struct dentry *dentry, struct kstatfs *stats)
{
	struct super_block *sb = dentry->d_sb;
	struct logfs_super *super = logfs_super(sb);

	stats->f_type		= LOGFS_MAGIC_U32;
	stats->f_bsize		= sb->s_blocksize;
	stats->f_blocks		= super->s_size >> LOGFS_BLOCK_BITS >> 3;
	stats->f_bfree		= super->s_free_bytes >> sb->s_blocksize_bits;
	stats->f_bavail		= super->s_free_bytes >> sb->s_blocksize_bits;
	stats->f_files		= 0;
	stats->f_ffree		= 0;
	stats->f_namelen	= LOGFS_MAX_NAMELEN;
	return 0;
}

static int logfs_sb_set(struct super_block *sb, void *_super)
{
	struct logfs_super *super = _super;

	sb->s_fs_info = super;
	sb->s_mtd = super->s_mtd;
	sb->s_bdev = super->s_bdev;
#ifdef CONFIG_BLOCK
	if (sb->s_bdev)
		sb->s_bdi = &bdev_get_queue(sb->s_bdev)->backing_dev_info;
#endif
#ifdef CONFIG_MTD
	if (sb->s_mtd)
		sb->s_bdi = sb->s_mtd->backing_dev_info;
#endif
	return 0;
}

static int logfs_sb_test(struct super_block *sb, void *_super)
{
	struct logfs_super *super = _super;
	struct mtd_info *mtd = super->s_mtd;

	if (mtd && sb->s_mtd == mtd)
		return 1;
	if (super->s_bdev && sb->s_bdev == super->s_bdev)
		return 1;
	return 0;
}

static void set_segment_header(struct logfs_segment_header *sh, u8 type,
		u8 level, u32 segno, u32 ec)
{
	sh->pad = 0;
	sh->type = type;
	sh->level = level;
	sh->segno = cpu_to_be32(segno);
	sh->ec = cpu_to_be32(ec);
	sh->gec = cpu_to_be64(segno);
	sh->crc = logfs_crc32(sh, LOGFS_SEGMENT_HEADERSIZE, 4);
}

static void logfs_write_ds(struct super_block *sb, struct logfs_disk_super *ds,
		u32 segno, u32 ec)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_segment_header *sh = &ds->ds_sh;
	int i;

	memset(ds, 0, sizeof(*ds));
	set_segment_header(sh, SEG_SUPER, 0, segno, ec);

	ds->ds_ifile_levels	= super->s_ifile_levels;
	ds->ds_iblock_levels	= super->s_iblock_levels;
	ds->ds_data_levels	= super->s_data_levels; /* XXX: Remove */
	ds->ds_segment_shift	= super->s_segshift;
	ds->ds_block_shift	= sb->s_blocksize_bits;
	ds->ds_write_shift	= super->s_writeshift;
	ds->ds_filesystem_size	= cpu_to_be64(super->s_size);
	ds->ds_segment_size	= cpu_to_be32(super->s_segsize);
	ds->ds_bad_seg_reserve	= cpu_to_be32(super->s_bad_seg_reserve);
	ds->ds_feature_incompat	= cpu_to_be64(super->s_feature_incompat);
	ds->ds_feature_ro_compat= cpu_to_be64(super->s_feature_ro_compat);
	ds->ds_feature_compat	= cpu_to_be64(super->s_feature_compat);
	ds->ds_feature_flags	= cpu_to_be64(super->s_feature_flags);
	ds->ds_root_reserve	= cpu_to_be64(super->s_root_reserve);
	ds->ds_speed_reserve	= cpu_to_be64(super->s_speed_reserve);
	journal_for_each(i)
		ds->ds_journal_seg[i] = cpu_to_be32(super->s_journal_seg[i]);
	ds->ds_magic		= cpu_to_be64(LOGFS_MAGIC);
	ds->ds_crc = logfs_crc32(ds, sizeof(*ds),
			LOGFS_SEGMENT_HEADERSIZE + 12);
}

static int write_one_sb(struct super_block *sb,
		struct page *(*find_sb)(struct super_block *sb, u64 *ofs))
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_disk_super *ds;
	struct logfs_segment_entry se;
	struct page *page;
	u64 ofs;
	u32 ec, segno;
	int err;

	page = find_sb(sb, &ofs);
	if (!page)
		return -EIO;
	ds = page_address(page);
	segno = seg_no(sb, ofs);
	logfs_get_segment_entry(sb, segno, &se);
	ec = be32_to_cpu(se.ec_level) >> 4;
	ec++;
	logfs_set_segment_erased(sb, segno, ec, 0);
	logfs_write_ds(sb, ds, segno, ec);
	err = super->s_devops->write_sb(sb, page);
	page_cache_release(page);
	return err;
}

int logfs_write_sb(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int err;

	/* First superblock */
	err = write_one_sb(sb, super->s_devops->find_first_sb);
	if (err)
		return err;

	/* Last superblock */
	err = write_one_sb(sb, super->s_devops->find_last_sb);
	if (err)
		return err;
	return 0;
}

static int ds_cmp(const void *ds0, const void *ds1)
{
	size_t len = sizeof(struct logfs_disk_super);

	/* We know the segment headers differ, so ignore them */
	len -= LOGFS_SEGMENT_HEADERSIZE;
	ds0 += LOGFS_SEGMENT_HEADERSIZE;
	ds1 += LOGFS_SEGMENT_HEADERSIZE;
	return memcmp(ds0, ds1, len);
}

static int logfs_recover_sb(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_disk_super _ds0, *ds0 = &_ds0;
	struct logfs_disk_super _ds1, *ds1 = &_ds1;
	int err, valid0, valid1;

	/* read first superblock */
	err = wbuf_read(sb, super->s_sb_ofs[0], sizeof(*ds0), ds0);
	if (err)
		return err;
	/* read last superblock */
	err = wbuf_read(sb, super->s_sb_ofs[1], sizeof(*ds1), ds1);
	if (err)
		return err;
	valid0 = logfs_check_ds(ds0) == 0;
	valid1 = logfs_check_ds(ds1) == 0;

	if (!valid0 && valid1) {
		printk(KERN_INFO"First superblock is invalid - fixing.\n");
		return write_one_sb(sb, super->s_devops->find_first_sb);
	}
	if (valid0 && !valid1) {
		printk(KERN_INFO"Last superblock is invalid - fixing.\n");
		return write_one_sb(sb, super->s_devops->find_last_sb);
	}
	if (valid0 && valid1 && ds_cmp(ds0, ds1)) {
		printk(KERN_INFO"Superblocks don't match - fixing.\n");
		return logfs_write_sb(sb);
	}
	/* If neither is valid now, something's wrong.  Didn't we properly
	 * check them before?!? */
	BUG_ON(!valid0 && !valid1);
	return 0;
}

static int logfs_make_writeable(struct super_block *sb)
{
	int err;

	err = logfs_open_segfile(sb);
	if (err)
		return err;

	/* Repair any broken superblock copies */
	err = logfs_recover_sb(sb);
	if (err)
		return err;

	/* Check areas for trailing unaccounted data */
	err = logfs_check_areas(sb);
	if (err)
		return err;

	/* Do one GC pass before any data gets dirtied */
	logfs_gc_pass(sb);

	/* after all initializations are done, replay the journal
	 * for rw-mounts, if necessary */
	err = logfs_replay_journal(sb);
	if (err)
		return err;

	return 0;
}

static int logfs_get_sb_final(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *rootdir;
	int err;

	/* root dir */
	rootdir = logfs_iget(sb, LOGFS_INO_ROOT);
	if (IS_ERR(rootdir))
		goto fail;

	sb->s_root = d_alloc_root(rootdir);
	if (!sb->s_root) {
		iput(rootdir);
		goto fail;
	}

	/* at that point we know that ->put_super() will be called */
	super->s_erase_page = alloc_pages(GFP_KERNEL, 0);
	if (!super->s_erase_page)
		return -ENOMEM;
	memset(page_address(super->s_erase_page), 0xFF, PAGE_SIZE);

	/* FIXME: check for read-only mounts */
	err = logfs_make_writeable(sb);
	if (err) {
		__free_page(super->s_erase_page);
		return err;
	}

	log_super("LogFS: Finished mounting\n");
	return 0;

fail:
	iput(super->s_master_inode);
	iput(super->s_segfile_inode);
	iput(super->s_mapping_inode);
	return -EIO;
}

int logfs_check_ds(struct logfs_disk_super *ds)
{
	struct logfs_segment_header *sh = &ds->ds_sh;

	if (ds->ds_magic != cpu_to_be64(LOGFS_MAGIC))
		return -EINVAL;
	if (sh->crc != logfs_crc32(sh, LOGFS_SEGMENT_HEADERSIZE, 4))
		return -EINVAL;
	if (ds->ds_crc != logfs_crc32(ds, sizeof(*ds),
				LOGFS_SEGMENT_HEADERSIZE + 12))
		return -EINVAL;
	return 0;
}

static struct page *find_super_block(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct page *first, *last;

	first = super->s_devops->find_first_sb(sb, &super->s_sb_ofs[0]);
	if (!first || IS_ERR(first))
		return NULL;
	last = super->s_devops->find_last_sb(sb, &super->s_sb_ofs[1]);
	if (!last || IS_ERR(last)) {
		page_cache_release(first);
		return NULL;
	}

	if (!logfs_check_ds(page_address(first))) {
		page_cache_release(last);
		return first;
	}

	/* First one didn't work, try the second superblock */
	if (!logfs_check_ds(page_address(last))) {
		page_cache_release(first);
		return last;
	}

	/* Neither worked, sorry folks */
	page_cache_release(first);
	page_cache_release(last);
	return NULL;
}

static int __logfs_read_sb(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct page *page;
	struct logfs_disk_super *ds;
	int i;

	page = find_super_block(sb);
	if (!page)
		return -EINVAL;

	ds = page_address(page);
	super->s_size = be64_to_cpu(ds->ds_filesystem_size);
	super->s_root_reserve = be64_to_cpu(ds->ds_root_reserve);
	super->s_speed_reserve = be64_to_cpu(ds->ds_speed_reserve);
	super->s_bad_seg_reserve = be32_to_cpu(ds->ds_bad_seg_reserve);
	super->s_segsize = 1 << ds->ds_segment_shift;
	super->s_segmask = (1 << ds->ds_segment_shift) - 1;
	super->s_segshift = ds->ds_segment_shift;
	sb->s_blocksize = 1 << ds->ds_block_shift;
	sb->s_blocksize_bits = ds->ds_block_shift;
	super->s_writesize = 1 << ds->ds_write_shift;
	super->s_writeshift = ds->ds_write_shift;
	super->s_no_segs = super->s_size >> super->s_segshift;
	super->s_no_blocks = super->s_segsize >> sb->s_blocksize_bits;
	super->s_feature_incompat = be64_to_cpu(ds->ds_feature_incompat);
	super->s_feature_ro_compat = be64_to_cpu(ds->ds_feature_ro_compat);
	super->s_feature_compat = be64_to_cpu(ds->ds_feature_compat);
	super->s_feature_flags = be64_to_cpu(ds->ds_feature_flags);

	journal_for_each(i)
		super->s_journal_seg[i] = be32_to_cpu(ds->ds_journal_seg[i]);

	super->s_ifile_levels = ds->ds_ifile_levels;
	super->s_iblock_levels = ds->ds_iblock_levels;
	super->s_data_levels = ds->ds_data_levels;
	super->s_total_levels = super->s_ifile_levels + super->s_iblock_levels
		+ super->s_data_levels;
	page_cache_release(page);
	return 0;
}

static int logfs_read_sb(struct super_block *sb, int read_only)
{
	struct logfs_super *super = logfs_super(sb);
	int ret;

	super->s_btree_pool = mempool_create(32, btree_alloc, btree_free, NULL);
	if (!super->s_btree_pool)
		return -ENOMEM;

	btree_init_mempool64(&super->s_shadow_tree.new, super->s_btree_pool);
	btree_init_mempool64(&super->s_shadow_tree.old, super->s_btree_pool);
	btree_init_mempool32(&super->s_shadow_tree.segment_map,
			super->s_btree_pool);

	ret = logfs_init_mapping(sb);
	if (ret)
		return ret;

	ret = __logfs_read_sb(sb);
	if (ret)
		return ret;

	if (super->s_feature_incompat & ~LOGFS_FEATURES_INCOMPAT)
		return -EIO;
	if ((super->s_feature_ro_compat & ~LOGFS_FEATURES_RO_COMPAT) &&
			!read_only)
		return -EIO;

	ret = logfs_init_rw(sb);
	if (ret)
		return ret;

	ret = logfs_init_areas(sb);
	if (ret)
		return ret;

	ret = logfs_init_gc(sb);
	if (ret)
		return ret;

	ret = logfs_init_journal(sb);
	if (ret)
		return ret;

	return 0;
}

static void logfs_kill_sb(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	log_super("LogFS: Start unmounting\n");
	/* Alias entries slow down mount, so evict as many as possible */
	sync_filesystem(sb);
	logfs_write_anchor(sb);
	free_areas(sb);

	/*
	 * From this point on alias entries are simply dropped - and any
	 * writes to the object store are considered bugs.
	 */
	log_super("LogFS: Now in shutdown\n");
	generic_shutdown_super(sb);
	super->s_flags |= LOGFS_SB_FLAG_SHUTDOWN;

	BUG_ON(super->s_dirty_used_bytes || super->s_dirty_free_bytes);

	logfs_cleanup_gc(sb);
	logfs_cleanup_journal(sb);
	logfs_cleanup_areas(sb);
	logfs_cleanup_rw(sb);
	if (super->s_erase_page)
		__free_page(super->s_erase_page);
	super->s_devops->put_device(super);
	logfs_mempool_destroy(super->s_btree_pool);
	logfs_mempool_destroy(super->s_alias_pool);
	kfree(super);
	log_super("LogFS: Finished unmounting\n");
}

static struct dentry *logfs_get_sb_device(struct logfs_super *super,
		struct file_system_type *type, int flags)
{
	struct super_block *sb;
	int err = -ENOMEM;
	static int mount_count;

	log_super("LogFS: Start mount %x\n", mount_count++);

	err = -EINVAL;
	sb = sget(type, logfs_sb_test, logfs_sb_set, super);
	if (IS_ERR(sb)) {
		super->s_devops->put_device(super);
		kfree(super);
		return ERR_CAST(sb);
	}

	if (sb->s_root) {
		/* Device is already in use */
		super->s_devops->put_device(super);
		kfree(super);
		return dget(sb->s_root);
	}

	/*
	 * sb->s_maxbytes is limited to 8TB.  On 32bit systems, the page cache
	 * only covers 16TB and the upper 8TB are used for indirect blocks.
	 * On 64bit system we could bump up the limit, but that would make
	 * the filesystem incompatible with 32bit systems.
	 */
	sb->s_maxbytes	= (1ull << 43) - 1;
	sb->s_op	= &logfs_super_operations;
	sb->s_flags	= flags | MS_NOATIME;

	err = logfs_read_sb(sb, sb->s_flags & MS_RDONLY);
	if (err)
		goto err1;

	sb->s_flags |= MS_ACTIVE;
	err = logfs_get_sb_final(sb);
	if (err) {
		deactivate_locked_super(sb);
		return ERR_PTR(err);
	}
	return dget(sb->s_root);

err1:
	/* no ->s_root, no ->put_super() */
	iput(super->s_master_inode);
	iput(super->s_segfile_inode);
	iput(super->s_mapping_inode);
	deactivate_locked_super(sb);
	return ERR_PTR(err);
}

static struct dentry *logfs_mount(struct file_system_type *type, int flags,
		const char *devname, void *data)
{
	ulong mtdnr;
	struct logfs_super *super;
	int err;

	super = kzalloc(sizeof(*super), GFP_KERNEL);
	if (!super)
		return ERR_PTR(-ENOMEM);

	mutex_init(&super->s_dirop_mutex);
	mutex_init(&super->s_object_alias_mutex);
	INIT_LIST_HEAD(&super->s_freeing_list);

	if (!devname)
		err = logfs_get_sb_bdev(super, type, devname);
	else if (strncmp(devname, "mtd", 3))
		err = logfs_get_sb_bdev(super, type, devname);
	else {
		char *garbage;
		mtdnr = simple_strtoul(devname+3, &garbage, 0);
		if (*garbage)
			err = -EINVAL;
		else
			err = logfs_get_sb_mtd(super, mtdnr);
	}

	if (err) {
		kfree(super);
		return ERR_PTR(err);
	}

	return logfs_get_sb_device(super, type, flags);
}

static struct file_system_type logfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "logfs",
	.mount		= logfs_mount,
	.kill_sb	= logfs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,

};

static int __init logfs_init(void)
{
	int ret;

	emergency_page = alloc_pages(GFP_KERNEL, 0);
	if (!emergency_page)
		return -ENOMEM;

	ret = logfs_compr_init();
	if (ret)
		goto out1;

	ret = logfs_init_inode_cache();
	if (ret)
		goto out2;

	return register_filesystem(&logfs_fs_type);
out2:
	logfs_compr_exit();
out1:
	__free_pages(emergency_page, 0);
	return ret;
}

static void __exit logfs_exit(void)
{
	unregister_filesystem(&logfs_fs_type);
	logfs_destroy_inode_cache();
	logfs_compr_exit();
	__free_pages(emergency_page, 0);
}

module_init(logfs_init);
module_exit(logfs_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joern Engel <joern@logfs.org>");
MODULE_DESCRIPTION("scalable flash filesystem");
