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
 * 30-06-1998 by Frank Denis : first step to write inodes.
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

static void qnx4_put_super(struct super_block *sb);
static struct inode *qnx4_alloc_inode(struct super_block *sb);
static void qnx4_destroy_inode(struct inode *inode);
static int qnx4_remount(struct super_block *sb, int *flags, char *data);
static int qnx4_statfs(struct dentry *, struct kstatfs *);

static const struct super_operations qnx4_sops =
{
	.alloc_inode	= qnx4_alloc_inode,
	.destroy_inode	= qnx4_destroy_inode,
	.put_super	= qnx4_put_super,
	.statfs		= qnx4_statfs,
	.remount_fs	= qnx4_remount,
};

static int qnx4_remount(struct super_block *sb, int *flags, char *data)
{
	struct qnx4_sb_info *qs;

	qs = qnx4_sb(sb);
	qs->Version = QNX4_VERSION;
	*flags |= MS_RDONLY;
	return 0;
}

static struct buffer_head *qnx4_getblk(struct inode *inode, int nr,
				       int create)
{
	struct buffer_head *result = NULL;

	if ( nr >= 0 )
		nr = qnx4_block_map( inode, nr );
	if (nr) {
		result = sb_getblk(inode->i_sb, nr);
		return result;
	}
	return NULL;
}

struct buffer_head *qnx4_bread(struct inode *inode, int block, int create)
{
	struct buffer_head *bh;

	bh = qnx4_getblk(inode, block, create);
	if (!bh || buffer_uptodate(bh)) {
		return bh;
	}
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh)) {
		return bh;
	}
	brelse(bh);

	return NULL;
}

static int qnx4_get_block( struct inode *inode, sector_t iblock, struct buffer_head *bh, int create )
{
	unsigned long phys;

	QNX4DEBUG((KERN_INFO "qnx4: qnx4_get_block inode=[%ld] iblock=[%ld]\n",inode->i_ino,iblock));

	phys = qnx4_block_map( inode, iblock );
	if ( phys ) {
		// logical block is before EOF
		map_bh(bh, inode->i_sb, phys);
	}
	return 0;
}

unsigned long qnx4_block_map( struct inode *inode, long iblock )
{
	int ix;
	long offset, i_xblk;
	unsigned long block = 0;
	struct buffer_head *bh = NULL;
	struct qnx4_xblk *xblk = NULL;
	struct qnx4_inode_entry *qnx4_inode = qnx4_raw_inode(inode);
	u16 nxtnt = le16_to_cpu(qnx4_inode->di_num_xtnts);

	if ( iblock < le32_to_cpu(qnx4_inode->di_first_xtnt.xtnt_size) ) {
		// iblock is in the first extent. This is easy.
		block = le32_to_cpu(qnx4_inode->di_first_xtnt.xtnt_blk) + iblock - 1;
	} else {
		// iblock is beyond first extent. We have to follow the extent chain.
		i_xblk = le32_to_cpu(qnx4_inode->di_xblk);
		offset = iblock - le32_to_cpu(qnx4_inode->di_first_xtnt.xtnt_size);
		ix = 0;
		while ( --nxtnt > 0 ) {
			if ( ix == 0 ) {
				// read next xtnt block.
				bh = sb_bread(inode->i_sb, i_xblk - 1);
				if ( !bh ) {
					QNX4DEBUG((KERN_ERR "qnx4: I/O error reading xtnt block [%ld])\n", i_xblk - 1));
					return -EIO;
				}
				xblk = (struct qnx4_xblk*)bh->b_data;
				if ( memcmp( xblk->xblk_signature, "IamXblk", 7 ) ) {
					QNX4DEBUG((KERN_ERR "qnx4: block at %ld is not a valid xtnt\n", qnx4_inode->i_xblk));
					return -EIO;
				}
			}
			if ( offset < le32_to_cpu(xblk->xblk_xtnts[ix].xtnt_size) ) {
				// got it!
				block = le32_to_cpu(xblk->xblk_xtnts[ix].xtnt_blk) + offset - 1;
				break;
			}
			offset -= le32_to_cpu(xblk->xblk_xtnts[ix].xtnt_size);
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

	QNX4DEBUG((KERN_INFO "qnx4: mapping block %ld of inode %ld = %ld\n",iblock,inode->i_ino,block));
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
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

/*
 * Check the root directory of the filesystem to make sure
 * it really _is_ a qnx4 filesystem, and to check the size
 * of the directory entry.
 */
static const char *qnx4_checkroot(struct super_block *sb)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *rootdir;
	int rd, rl;
	int i, j;
	int found = 0;

	if (*(qnx4_sb(sb)->sb->RootDir.di_fname) != '/') {
		return "no qnx4 filesystem (no root dir).";
	} else {
		QNX4DEBUG((KERN_NOTICE "QNX4 filesystem found on dev %s.\n", sb->s_id));
		rd = le32_to_cpu(qnx4_sb(sb)->sb->RootDir.di_first_xtnt.xtnt_blk) - 1;
		rl = le32_to_cpu(qnx4_sb(sb)->sb->RootDir.di_first_xtnt.xtnt_size);
		for (j = 0; j < rl; j++) {
			bh = sb_bread(sb, rd + j);	/* root dir, first block */
			if (bh == NULL) {
				return "unable to read root entry.";
			}
			for (i = 0; i < QNX4_INODES_PER_BLOCK; i++) {
				rootdir = (struct qnx4_inode_entry *) (bh->b_data + i * QNX4_DIR_ENTRY_SIZE);
				if (rootdir->di_fname != NULL) {
					QNX4DEBUG((KERN_INFO "rootdir entry found : [%s]\n", rootdir->di_fname));
					if (!strcmp(rootdir->di_fname,
						    QNX4_BMNAME)) {
						found = 1;
						qnx4_sb(sb)->BitMap = kmalloc( sizeof( struct qnx4_inode_entry ), GFP_KERNEL );
						if (!qnx4_sb(sb)->BitMap) {
							brelse (bh);
							return "not enough memory for bitmap inode";
						}
						memcpy( qnx4_sb(sb)->BitMap, rootdir, sizeof( struct qnx4_inode_entry ) );	/* keep bitmap inode known */
						break;
					}
				}
			}
			brelse(bh);
			if (found != 0) {
				break;
			}
		}
		if (found == 0) {
			return "bitmap file not found.";
		}
	}
	return NULL;
}

static int qnx4_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct inode *root;
	const char *errmsg;
	struct qnx4_sb_info *qs;
	int ret = -EINVAL;

	qs = kzalloc(sizeof(struct qnx4_sb_info), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;
	s->s_fs_info = qs;

	sb_set_blocksize(s, QNX4_BLOCK_SIZE);

	/* Check the superblock signature. Since the qnx4 code is
	   dangerous, we should leave as quickly as possible
	   if we don't belong here... */
	bh = sb_bread(s, 1);
	if (!bh) {
		printk(KERN_ERR "qnx4: unable to read the superblock\n");
		goto outnobh;
	}
	if ( le32_to_cpup((__le32*) bh->b_data) != QNX4_SUPER_MAGIC ) {
		if (!silent)
			printk(KERN_ERR "qnx4: wrong fsid in superblock.\n");
		goto out;
	}
	s->s_op = &qnx4_sops;
	s->s_magic = QNX4_SUPER_MAGIC;
	s->s_flags |= MS_RDONLY;	/* Yup, read-only yet */
	qnx4_sb(s)->sb_buf = bh;
	qnx4_sb(s)->sb = (struct qnx4_super_block *) bh->b_data;


 	/* check before allocating dentries, inodes, .. */
	errmsg = qnx4_checkroot(s);
	if (errmsg != NULL) {
 		if (!silent)
			printk(KERN_ERR "qnx4: %s\n", errmsg);
		goto out;
	}

 	/* does root not have inode number QNX4_ROOT_INO ?? */
	root = qnx4_iget(s, QNX4_ROOT_INO * QNX4_INODES_PER_BLOCK);
	if (IS_ERR(root)) {
		printk(KERN_ERR "qnx4: get inode failed\n");
		ret = PTR_ERR(root);
 		goto out;
 	}

	ret = -ENOMEM;
 	s->s_root = d_alloc_root(root);
 	if (s->s_root == NULL)
 		goto outi;

	brelse(bh);
	return 0;

      outi:
	iput(root);
      out:
	brelse(bh);
      outnobh:
	kfree(qs);
	s->s_fs_info = NULL;
	return ret;
}

static void qnx4_put_super(struct super_block *sb)
{
	struct qnx4_sb_info *qs = qnx4_sb(sb);
	kfree( qs->BitMap );
	kfree( qs );
	sb->s_fs_info = NULL;
	return;
}

static int qnx4_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page,qnx4_get_block, wbc);
}

static int qnx4_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,qnx4_get_block);
}

static int qnx4_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct qnx4_inode_info *qnx4_inode = qnx4_i(mapping->host);
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				qnx4_get_block,
				&qnx4_inode->mmu_private);
	if (unlikely(ret)) {
		loff_t isize = mapping->host->i_size;
		if (pos + len > isize)
			vmtruncate(mapping->host, isize);
	}

	return ret;
}
static sector_t qnx4_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,qnx4_get_block);
}
static const struct address_space_operations qnx4_aops = {
	.readpage	= qnx4_readpage,
	.writepage	= qnx4_writepage,
	.write_begin	= qnx4_write_begin,
	.write_end	= generic_write_end,
	.bmap		= qnx4_bmap
};

struct inode *qnx4_iget(struct super_block *sb, unsigned long ino)
{
	struct buffer_head *bh;
	struct qnx4_inode_entry *raw_inode;
	int block;
	struct qnx4_inode_entry *qnx4_inode;
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	qnx4_inode = qnx4_raw_inode(inode);
	inode->i_mode = 0;

	QNX4DEBUG((KERN_INFO "reading inode : [%d]\n", ino));
	if (!ino) {
		printk(KERN_ERR "qnx4: bad inode number on dev %s: %lu is "
				"out of range\n",
		       sb->s_id, ino);
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}
	block = ino / QNX4_INODES_PER_BLOCK;

	if (!(bh = sb_bread(sb, block))) {
		printk(KERN_ERR "qnx4: major problem: unable to read inode from dev "
		       "%s\n", sb->s_id);
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}
	raw_inode = ((struct qnx4_inode_entry *) bh->b_data) +
	    (ino % QNX4_INODES_PER_BLOCK);

	inode->i_mode    = le16_to_cpu(raw_inode->di_mode);
	inode->i_uid     = (uid_t)le16_to_cpu(raw_inode->di_uid);
	inode->i_gid     = (gid_t)le16_to_cpu(raw_inode->di_gid);
	inode->i_nlink   = le16_to_cpu(raw_inode->di_nlink);
	inode->i_size    = le32_to_cpu(raw_inode->di_size);
	inode->i_mtime.tv_sec   = le32_to_cpu(raw_inode->di_mtime);
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_sec   = le32_to_cpu(raw_inode->di_atime);
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_sec   = le32_to_cpu(raw_inode->di_ctime);
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks  = le32_to_cpu(raw_inode->di_first_xtnt.xtnt_size);

	memcpy(qnx4_inode, raw_inode, QNX4_DIR_ENTRY_SIZE);
	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &generic_ro_fops;
		inode->i_mapping->a_ops = &qnx4_aops;
		qnx4_i(inode)->mmu_private = inode->i_size;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &qnx4_dir_inode_operations;
		inode->i_fop = &qnx4_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &page_symlink_inode_operations;
		inode->i_mapping->a_ops = &qnx4_aops;
		qnx4_i(inode)->mmu_private = inode->i_size;
	} else {
		printk(KERN_ERR "qnx4: bad inode %lu on dev %s\n",
			ino, sb->s_id);
		iget_failed(inode);
		brelse(bh);
		return ERR_PTR(-EIO);
	}
	brelse(bh);
	unlock_new_inode(inode);
	return inode;
}

static struct kmem_cache *qnx4_inode_cachep;

static struct inode *qnx4_alloc_inode(struct super_block *sb)
{
	struct qnx4_inode_info *ei;
	ei = kmem_cache_alloc(qnx4_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void qnx4_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	INIT_LIST_HEAD(&inode->i_dentry);
	kmem_cache_free(qnx4_inode_cachep, qnx4_i(inode));
}

static void qnx4_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, qnx4_i_callback);
}

static void init_once(void *foo)
{
	struct qnx4_inode_info *ei = (struct qnx4_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	qnx4_inode_cachep = kmem_cache_create("qnx4_inode_cache",
					     sizeof(struct qnx4_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (qnx4_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(qnx4_inode_cachep);
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
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_qnx4_fs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		return err;

	err = register_filesystem(&qnx4_fs_type);
	if (err) {
		destroy_inodecache();
		return err;
	}

	printk(KERN_INFO "QNX4 filesystem 0.2.3 registered.\n");
	return 0;
}

static void __exit exit_qnx4_fs(void)
{
	unregister_filesystem(&qnx4_fs_type);
	destroy_inodecache();
}

module_init(init_qnx4_fs)
module_exit(exit_qnx4_fs)
MODULE_LICENSE("GPL");

