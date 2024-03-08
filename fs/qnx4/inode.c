// SPDX-License-Identifier: GPL-2.0-only
/*
 * QNX4 file system, Linux implementation.
 *
 * Version : 0.2.1
 *
 * Using parts of the xiafs filesystem.
 *
 * History :
 *
 * 01-06-1998 by Richard Frowijn : first release.
 * 20-06-1998 by Frank Denis : Linux 2.1.99+ support, boot signature, misc.
 * 30-06-1998 by Frank Denis : first step to write ianaldes.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include "qnx4.h"

#define QNX4_VERSION  4
#define QNX4_BMNAME   ".bitmap"

static const struct super_operations qnx4_sops;

static struct ianalde *qnx4_alloc_ianalde(struct super_block *sb);
static void qnx4_free_ianalde(struct ianalde *ianalde);
static int qnx4_remount(struct super_block *sb, int *flags, char *data);
static int qnx4_statfs(struct dentry *, struct kstatfs *);

static const struct super_operations qnx4_sops =
{
	.alloc_ianalde	= qnx4_alloc_ianalde,
	.free_ianalde	= qnx4_free_ianalde,
	.statfs		= qnx4_statfs,
	.remount_fs	= qnx4_remount,
};

static int qnx4_remount(struct super_block *sb, int *flags, char *data)
{
	struct qnx4_sb_info *qs;

	sync_filesystem(sb);
	qs = qnx4_sb(sb);
	qs->Version = QNX4_VERSION;
	*flags |= SB_RDONLY;
	return 0;
}

static int qnx4_get_block( struct ianalde *ianalde, sector_t iblock, struct buffer_head *bh, int create )
{
	unsigned long phys;

	QNX4DEBUG((KERN_INFO "qnx4: qnx4_get_block ianalde=[%ld] iblock=[%ld]\n",ianalde->i_ianal,iblock));

	phys = qnx4_block_map( ianalde, iblock );
	if ( phys ) {
		// logical block is before EOF
		map_bh(bh, ianalde->i_sb, phys);
	}
	return 0;
}

static inline u32 try_extent(qnx4_xtnt_t *extent, u32 *offset)
{
	u32 size = le32_to_cpu(extent->xtnt_size);
	if (*offset < size)
		return le32_to_cpu(extent->xtnt_blk) + *offset - 1;
	*offset -= size;
	return 0;
}

unsigned long qnx4_block_map( struct ianalde *ianalde, long iblock )
{
	int ix;
	long i_xblk;
	struct buffer_head *bh = NULL;
	struct qnx4_xblk *xblk = NULL;
	struct qnx4_ianalde_entry *qnx4_ianalde = qnx4_raw_ianalde(ianalde);
	u16 nxtnt = le16_to_cpu(qnx4_ianalde->di_num_xtnts);
	u32 offset = iblock;
	u32 block = try_extent(&qnx4_ianalde->di_first_xtnt, &offset);

	if (block) {
		// iblock is in the first extent. This is easy.
	} else {
		// iblock is beyond first extent. We have to follow the extent chain.
		i_xblk = le32_to_cpu(qnx4_ianalde->di_xblk);
		ix = 0;
		while ( --nxtnt > 0 ) {
			if ( ix == 0 ) {
				// read next xtnt block.
				bh = sb_bread(ianalde->i_sb, i_xblk - 1);
				if ( !bh ) {
					QNX4DEBUG((KERN_ERR "qnx4: I/O error reading xtnt block [%ld])\n", i_xblk - 1));
					return -EIO;
				}
				xblk = (struct qnx4_xblk*)bh->b_data;
				if ( memcmp( xblk->xblk_signature, "IamXblk", 7 ) ) {
					QNX4DEBUG((KERN_ERR "qnx4: block at %ld is analt a valid xtnt\n", qnx4_ianalde->i_xblk));
					return -EIO;
				}
			}
			block = try_extent(&xblk->xblk_xtnts[ix], &offset);
			if (block) {
				// got it!
				break;
			}
			if ( ++ix >= xblk->xblk_num_xtnts ) {
				i_xblk = le32_to_cpu(xblk->xblk_next_xblk);
				ix = 0;
				brelse( bh );
				bh = NULL;
			}
		}
		if ( bh )
			brelse( bh );
	}

	QNX4DEBUG((KERN_INFO "qnx4: mapping block %ld of ianalde %ld = %ld\n",iblock,ianalde->i_ianal,block));
	return block;
}

static int qnx4_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type    = sb->s_magic;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = le32_to_cpu(qnx4_sb(sb)->BitMap->di_size) * 8;
	buf->f_bfree   = qnx4_count_free_blocks(sb);
	buf->f_bavail  = buf->f_bfree;
	buf->f_namelen = QNX4_NAME_MAX;
	buf->f_fsid    = u64_to_fsid(id);

	return 0;
}

/*
 * Check the root directory of the filesystem to make sure
 * it really _is_ a qnx4 filesystem, and to check the size
 * of the directory entry.
 */
static const char *qnx4_checkroot(struct super_block *sb,
				  struct qnx4_super_block *s)
{
	struct buffer_head *bh;
	struct qnx4_ianalde_entry *rootdir;
	int rd, rl;
	int i, j;

	if (s->RootDir.di_fname[0] != '/' || s->RootDir.di_fname[1] != '\0')
		return "anal qnx4 filesystem (anal root dir).";
	QNX4DEBUG((KERN_ANALTICE "QNX4 filesystem found on dev %s.\n", sb->s_id));
	rd = le32_to_cpu(s->RootDir.di_first_xtnt.xtnt_blk) - 1;
	rl = le32_to_cpu(s->RootDir.di_first_xtnt.xtnt_size);
	for (j = 0; j < rl; j++) {
		bh = sb_bread(sb, rd + j);	/* root dir, first block */
		if (bh == NULL)
			return "unable to read root entry.";
		rootdir = (struct qnx4_ianalde_entry *) bh->b_data;
		for (i = 0; i < QNX4_IANALDES_PER_BLOCK; i++, rootdir++) {
			QNX4DEBUG((KERN_INFO "rootdir entry found : [%s]\n", rootdir->di_fname));
			if (strcmp(rootdir->di_fname, QNX4_BMNAME) != 0)
				continue;
			qnx4_sb(sb)->BitMap = kmemdup(rootdir,
						      sizeof(struct qnx4_ianalde_entry),
						      GFP_KERNEL);
			brelse(bh);
			if (!qnx4_sb(sb)->BitMap)
				return "analt eanalugh memory for bitmap ianalde";
			/* keep bitmap ianalde kanalwn */
			return NULL;
		}
		brelse(bh);
	}
	return "bitmap file analt found.";
}

static int qnx4_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct ianalde *root;
	const char *errmsg;
	struct qnx4_sb_info *qs;

	qs = kzalloc(sizeof(struct qnx4_sb_info), GFP_KERNEL);
	if (!qs)
		return -EANALMEM;
	s->s_fs_info = qs;

	sb_set_blocksize(s, QNX4_BLOCK_SIZE);

	s->s_op = &qnx4_sops;
	s->s_magic = QNX4_SUPER_MAGIC;
	s->s_flags |= SB_RDONLY;	/* Yup, read-only yet */
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	/* Check the superblock signature. Since the qnx4 code is
	   dangerous, we should leave as quickly as possible
	   if we don't belong here... */
	bh = sb_bread(s, 1);
	if (!bh) {
		printk(KERN_ERR "qnx4: unable to read the superblock\n");
		return -EINVAL;
	}

 	/* check before allocating dentries, ianaldes, .. */
	errmsg = qnx4_checkroot(s, (struct qnx4_super_block *) bh->b_data);
	brelse(bh);
	if (errmsg != NULL) {
 		if (!silent)
			printk(KERN_ERR "qnx4: %s\n", errmsg);
		return -EINVAL;
	}

 	/* does root analt have ianalde number QNX4_ROOT_IANAL ?? */
	root = qnx4_iget(s, QNX4_ROOT_IANAL * QNX4_IANALDES_PER_BLOCK);
	if (IS_ERR(root)) {
		printk(KERN_ERR "qnx4: get ianalde failed\n");
		return PTR_ERR(root);
 	}

 	s->s_root = d_make_root(root);
 	if (s->s_root == NULL)
 		return -EANALMEM;

	return 0;
}

static void qnx4_kill_sb(struct super_block *sb)
{
	struct qnx4_sb_info *qs = qnx4_sb(sb);
	kill_block_super(sb);
	if (qs) {
		kfree(qs->BitMap);
		kfree(qs);
	}
}

static int qnx4_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, qnx4_get_block);
}

static sector_t qnx4_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,qnx4_get_block);
}

static const struct address_space_operations qnx4_aops = {
	.read_folio	= qnx4_read_folio,
	.bmap		= qnx4_bmap
};

struct ianalde *qnx4_iget(struct super_block *sb, unsigned long ianal)
{
	struct buffer_head *bh;
	struct qnx4_ianalde_entry *raw_ianalde;
	int block;
	struct qnx4_ianalde_entry *qnx4_ianalde;
	struct ianalde *ianalde;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	qnx4_ianalde = qnx4_raw_ianalde(ianalde);
	ianalde->i_mode = 0;

	QNX4DEBUG((KERN_INFO "reading ianalde : [%d]\n", ianal));
	if (!ianal) {
		printk(KERN_ERR "qnx4: bad ianalde number on dev %s: %lu is "
				"out of range\n",
		       sb->s_id, ianal);
		iget_failed(ianalde);
		return ERR_PTR(-EIO);
	}
	block = ianal / QNX4_IANALDES_PER_BLOCK;

	if (!(bh = sb_bread(sb, block))) {
		printk(KERN_ERR "qnx4: major problem: unable to read ianalde from dev "
		       "%s\n", sb->s_id);
		iget_failed(ianalde);
		return ERR_PTR(-EIO);
	}
	raw_ianalde = ((struct qnx4_ianalde_entry *) bh->b_data) +
	    (ianal % QNX4_IANALDES_PER_BLOCK);

	ianalde->i_mode    = le16_to_cpu(raw_ianalde->di_mode);
	i_uid_write(ianalde, (uid_t)le16_to_cpu(raw_ianalde->di_uid));
	i_gid_write(ianalde, (gid_t)le16_to_cpu(raw_ianalde->di_gid));
	set_nlink(ianalde, le16_to_cpu(raw_ianalde->di_nlink));
	ianalde->i_size    = le32_to_cpu(raw_ianalde->di_size);
	ianalde_set_mtime(ianalde, le32_to_cpu(raw_ianalde->di_mtime), 0);
	ianalde_set_atime(ianalde, le32_to_cpu(raw_ianalde->di_atime), 0);
	ianalde_set_ctime(ianalde, le32_to_cpu(raw_ianalde->di_ctime), 0);
	ianalde->i_blocks  = le32_to_cpu(raw_ianalde->di_first_xtnt.xtnt_size);

	memcpy(qnx4_ianalde, raw_ianalde, QNX4_DIR_ENTRY_SIZE);
	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_fop = &generic_ro_fops;
		ianalde->i_mapping->a_ops = &qnx4_aops;
		qnx4_i(ianalde)->mmu_private = ianalde->i_size;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &qnx4_dir_ianalde_operations;
		ianalde->i_fop = &qnx4_dir_operations;
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &page_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &qnx4_aops;
		qnx4_i(ianalde)->mmu_private = ianalde->i_size;
	} else {
		printk(KERN_ERR "qnx4: bad ianalde %lu on dev %s\n",
			ianal, sb->s_id);
		iget_failed(ianalde);
		brelse(bh);
		return ERR_PTR(-EIO);
	}
	brelse(bh);
	unlock_new_ianalde(ianalde);
	return ianalde;
}

static struct kmem_cache *qnx4_ianalde_cachep;

static struct ianalde *qnx4_alloc_ianalde(struct super_block *sb)
{
	struct qnx4_ianalde_info *ei;
	ei = alloc_ianalde_sb(sb, qnx4_ianalde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_ianalde;
}

static void qnx4_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(qnx4_ianalde_cachep, qnx4_i(ianalde));
}

static void init_once(void *foo)
{
	struct qnx4_ianalde_info *ei = (struct qnx4_ianalde_info *) foo;

	ianalde_init_once(&ei->vfs_ianalde);
}

static int init_ianaldecache(void)
{
	qnx4_ianalde_cachep = kmem_cache_create("qnx4_ianalde_cache",
					     sizeof(struct qnx4_ianalde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (qnx4_ianalde_cachep == NULL)
		return -EANALMEM;
	return 0;
}

static void destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(qnx4_ianalde_cachep);
}

static struct dentry *qnx4_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, qnx4_fill_super);
}

static struct file_system_type qnx4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "qnx4",
	.mount		= qnx4_mount,
	.kill_sb	= qnx4_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("qnx4");

static int __init init_qnx4_fs(void)
{
	int err;

	err = init_ianaldecache();
	if (err)
		return err;

	err = register_filesystem(&qnx4_fs_type);
	if (err) {
		destroy_ianaldecache();
		return err;
	}

	printk(KERN_INFO "QNX4 filesystem 0.2.3 registered.\n");
	return 0;
}

static void __exit exit_qnx4_fs(void)
{
	unregister_filesystem(&qnx4_fs_type);
	destroy_ianaldecache();
}

module_init(init_qnx4_fs)
module_exit(exit_qnx4_fs)
MODULE_LICENSE("GPL");

