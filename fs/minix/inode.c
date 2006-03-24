/*
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Copyright (C) 1996  Gertjan van Wingerde    (gertjan@cs.vu.nl)
 *	Minix V2 fs support.
 *
 *  Modified for 680x0 by Andreas Schwab
 */

#include <linux/module.h>
#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/vfs.h>

static void minix_read_inode(struct inode * inode);
static int minix_write_inode(struct inode * inode, int wait);
static int minix_statfs(struct super_block *sb, struct kstatfs *buf);
static int minix_remount (struct super_block * sb, int * flags, char * data);

static void minix_delete_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	inode->i_size = 0;
	minix_truncate(inode);
	minix_free_inode(inode);
}

static void minix_put_super(struct super_block *sb)
{
	int i;
	struct minix_sb_info *sbi = minix_sb(sb);

	if (!(sb->s_flags & MS_RDONLY)) {
		sbi->s_ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	}
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	brelse (sbi->s_sbh);
	kfree(sbi->s_imap);
	sb->s_fs_info = NULL;
	kfree(sbi);

	return;
}

static kmem_cache_t * minix_inode_cachep;

static struct inode *minix_alloc_inode(struct super_block *sb)
{
	struct minix_inode_info *ei;
	ei = (struct minix_inode_info *)kmem_cache_alloc(minix_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void minix_destroy_inode(struct inode *inode)
{
	kmem_cache_free(minix_inode_cachep, minix_i(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct minix_inode_info *ei = (struct minix_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ei->vfs_inode);
}
 
static int init_inodecache(void)
{
	minix_inode_cachep = kmem_cache_create("minix_inode_cache",
					     sizeof(struct minix_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (minix_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(minix_inode_cachep))
		printk(KERN_INFO "minix_inode_cache: not all structures were freed\n");
}

static struct super_operations minix_sops = {
	.alloc_inode	= minix_alloc_inode,
	.destroy_inode	= minix_destroy_inode,
	.read_inode	= minix_read_inode,
	.write_inode	= minix_write_inode,
	.delete_inode	= minix_delete_inode,
	.put_super	= minix_put_super,
	.statfs		= minix_statfs,
	.remount_fs	= minix_remount,
};

static int minix_remount (struct super_block * sb, int * flags, char * data)
{
	struct minix_sb_info * sbi = minix_sb(sb);
	struct minix_super_block * ms;

	ms = sbi->s_ms;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		if (ms->s_state & MINIX_VALID_FS ||
		    !(sbi->s_mount_state & MINIX_VALID_FS))
			return 0;
		/* Mounting a rw partition read-only. */
		ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	} else {
	  	/* Mount a partition which is read-only, read-write. */
		sbi->s_mount_state = ms->s_state;
		ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(sbi->s_sbh);

		if (!(sbi->s_mount_state & MINIX_VALID_FS))
			printk ("MINIX-fs warning: remounting unchecked fs, "
				"running fsck is recommended.\n");
		else if ((sbi->s_mount_state & MINIX_ERROR_FS))
			printk ("MINIX-fs warning: remounting fs with errors, "
				"running fsck is recommended.\n");
	}
	return 0;
}

static int minix_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct minix_super_block *ms;
	int i, block;
	struct inode *root_inode;
	struct minix_sb_info *sbi;

	sbi = kmalloc(sizeof(struct minix_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;
	memset(sbi, 0, sizeof(struct minix_sb_info));

	/* N.B. These should be compile-time tests.
	   Unfortunately that is impossible. */
	if (32 != sizeof (struct minix_inode))
		panic("bad V1 i-node size");
	if (64 != sizeof(struct minix2_inode))
		panic("bad V2 i-node size");

	if (!sb_set_blocksize(s, BLOCK_SIZE))
		goto out_bad_hblock;

	if (!(bh = sb_bread(s, 1)))
		goto out_bad_sb;

	ms = (struct minix_super_block *) bh->b_data;
	sbi->s_ms = ms;
	sbi->s_sbh = bh;
	sbi->s_mount_state = ms->s_state;
	sbi->s_ninodes = ms->s_ninodes;
	sbi->s_nzones = ms->s_nzones;
	sbi->s_imap_blocks = ms->s_imap_blocks;
	sbi->s_zmap_blocks = ms->s_zmap_blocks;
	sbi->s_firstdatazone = ms->s_firstdatazone;
	sbi->s_log_zone_size = ms->s_log_zone_size;
	sbi->s_max_size = ms->s_max_size;
	s->s_magic = ms->s_magic;
	if (s->s_magic == MINIX_SUPER_MAGIC) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		sbi->s_link_max = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		sbi->s_link_max = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		sbi->s_link_max = MINIX2_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		sbi->s_link_max = MINIX2_LINK_MAX;
	} else
		goto out_no_fs;

	/*
	 * Allocate the buffer map to keep the superblock small.
	 */
	i = (sbi->s_imap_blocks + sbi->s_zmap_blocks) * sizeof(bh);
	map = kmalloc(i, GFP_KERNEL);
	if (!map)
		goto out_no_map;
	memset(map, 0, i);
	sbi->s_imap = &map[0];
	sbi->s_zmap = &map[sbi->s_imap_blocks];

	block=2;
	for (i=0 ; i < sbi->s_imap_blocks ; i++) {
		if (!(sbi->s_imap[i]=sb_bread(s, block)))
			goto out_no_bitmap;
		block++;
	}
	for (i=0 ; i < sbi->s_zmap_blocks ; i++) {
		if (!(sbi->s_zmap[i]=sb_bread(s, block)))
			goto out_no_bitmap;
		block++;
	}

	minix_set_bit(0,sbi->s_imap[0]->b_data);
	minix_set_bit(0,sbi->s_zmap[0]->b_data);

	/* set up enough so that it can read an inode */
	s->s_op = &minix_sops;
	root_inode = iget(s, MINIX_ROOT_INO);
	if (!root_inode || is_bad_inode(root_inode))
		goto out_no_root;

	s->s_root = d_alloc_root(root_inode);
	if (!s->s_root)
		goto out_iput;

	if (!NO_TRUNCATE)
		s->s_root->d_op = &minix_dentry_operations;

	if (!(s->s_flags & MS_RDONLY)) {
		ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(bh);
	}
	if (!(sbi->s_mount_state & MINIX_VALID_FS))
		printk ("MINIX-fs: mounting unchecked file system, "
			"running fsck is recommended.\n");
 	else if (sbi->s_mount_state & MINIX_ERROR_FS)
		printk ("MINIX-fs: mounting file system with errors, "
			"running fsck is recommended.\n");
	return 0;

out_iput:
	iput(root_inode);
	goto out_freemap;

out_no_root:
	if (!silent)
		printk("MINIX-fs: get root inode failed\n");
	goto out_freemap;

out_no_bitmap:
	printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
    out_freemap:
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	kfree(sbi->s_imap);
	goto out_release;

out_no_map:
	if (!silent)
		printk ("MINIX-fs: can't allocate map\n");
	goto out_release;

out_no_fs:
	if (!silent)
		printk("VFS: Can't find a Minix or Minix V2 filesystem on device "
		       "%s.\n", s->s_id);
    out_release:
	brelse(bh);
	goto out;

out_bad_hblock:
	printk("MINIX-fs: blocksize too small for device.\n");
	goto out;

out_bad_sb:
	printk("MINIX-fs: unable to read superblock\n");
 out:
	s->s_fs_info = NULL;
	kfree(sbi);
	return -EINVAL;
}

static int minix_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (sbi->s_nzones - sbi->s_firstdatazone) << sbi->s_log_zone_size;
	buf->f_bfree = minix_count_free_blocks(sbi);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_ninodes;
	buf->f_ffree = minix_count_free_inodes(sbi);
	buf->f_namelen = sbi->s_namelen;
	return 0;
}

static int minix_get_block(struct inode *inode, sector_t block,
		    struct buffer_head *bh_result, int create)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_get_block(inode, block, bh_result, create);
	else
		return V2_minix_get_block(inode, block, bh_result, create);
}

static int minix_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, minix_get_block, wbc);
}
static int minix_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,minix_get_block);
}
static int minix_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_prepare_write(page,from,to,minix_get_block);
}
static sector_t minix_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,minix_get_block);
}
static struct address_space_operations minix_aops = {
	.readpage = minix_readpage,
	.writepage = minix_writepage,
	.sync_page = block_sync_page,
	.prepare_write = minix_prepare_write,
	.commit_write = generic_commit_write,
	.bmap = minix_bmap
};

static struct inode_operations minix_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.getattr	= minix_getattr,
};

void minix_set_inode(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &minix_file_inode_operations;
		inode->i_fop = &minix_file_operations;
		inode->i_mapping->a_ops = &minix_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &minix_dir_inode_operations;
		inode->i_fop = &minix_dir_operations;
		inode->i_mapping->a_ops = &minix_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &minix_symlink_inode_operations;
		inode->i_mapping->a_ops = &minix_aops;
	} else
		init_special_inode(inode, inode->i_mode, rdev);
}

/*
 * The minix V1 function to read an inode.
 */
static void V1_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V1_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode) {
		make_bad_inode(inode);
		return;
	}
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = (uid_t)raw_inode->i_uid;
	inode->i_gid = (gid_t)raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = raw_inode->i_time;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = inode->i_blksize = 0;
	for (i = 0; i < 9; i++)
		minix_inode->u.i1_data[i] = raw_inode->i_zone[i];
	minix_set_inode(inode, old_decode_dev(raw_inode->i_zone[0]));
	brelse(bh);
}

/*
 * The minix V2 function to read an inode.
 */
static void V2_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V2_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode) {
		make_bad_inode(inode);
		return;
	}
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = (uid_t)raw_inode->i_uid;
	inode->i_gid = (gid_t)raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime.tv_sec = raw_inode->i_mtime;
	inode->i_atime.tv_sec = raw_inode->i_atime;
	inode->i_ctime.tv_sec = raw_inode->i_ctime;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = inode->i_blksize = 0;
	for (i = 0; i < 10; i++)
		minix_inode->u.i2_data[i] = raw_inode->i_zone[i];
	minix_set_inode(inode, old_decode_dev(raw_inode->i_zone[0]));
	brelse(bh);
}

/*
 * The global function to read an inode.
 */
static void minix_read_inode(struct inode * inode)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		V1_minix_read_inode(inode);
	else
		V2_minix_read_inode(inode);
}

/*
 * The minix V1 function to synchronize an inode.
 */
static struct buffer_head * V1_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V1_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode)
		return NULL;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = fs_high2lowuid(inode->i_uid);
	raw_inode->i_gid = fs_high2lowgid(inode->i_gid);
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime.tv_sec;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = old_encode_dev(inode->i_rdev);
	else for (i = 0; i < 9; i++)
		raw_inode->i_zone[i] = minix_inode->u.i1_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

/*
 * The minix V2 function to synchronize an inode.
 */
static struct buffer_head * V2_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V2_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode)
		return NULL;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = fs_high2lowuid(inode->i_uid);
	raw_inode->i_gid = fs_high2lowgid(inode->i_gid);
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_mtime = inode->i_mtime.tv_sec;
	raw_inode->i_atime = inode->i_atime.tv_sec;
	raw_inode->i_ctime = inode->i_ctime.tv_sec;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = old_encode_dev(inode->i_rdev);
	else for (i = 0; i < 10; i++)
		raw_inode->i_zone[i] = minix_inode->u.i2_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

static struct buffer_head *minix_update_inode(struct inode *inode)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_update_inode(inode);
	else
		return V2_minix_update_inode(inode);
}

static int minix_write_inode(struct inode * inode, int wait)
{
	brelse(minix_update_inode(inode));
	return 0;
}

int minix_sync_inode(struct inode * inode)
{
	int err = 0;
	struct buffer_head *bh;

	bh = minix_update_inode(inode);
	if (bh && buffer_dirty(bh))
	{
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
		{
			printk ("IO error syncing minix inode [%s:%08lx]\n",
				inode->i_sb->s_id, inode->i_ino);
			err = -1;
		}
	}
	else if (!bh)
		err = -1;
	brelse (bh);
	return err;
}

int minix_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	generic_fillattr(dentry->d_inode, stat);
	if (INODE_VERSION(dentry->d_inode) == MINIX_V1)
		stat->blocks = (BLOCK_SIZE / 512) * V1_minix_blocks(stat->size);
	else
		stat->blocks = (BLOCK_SIZE / 512) * V2_minix_blocks(stat->size);
	stat->blksize = BLOCK_SIZE;
	return 0;
}

/*
 * The function that is called for file truncation.
 */
void minix_truncate(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;
	if (INODE_VERSION(inode) == MINIX_V1)
		V1_minix_truncate(inode);
	else
		V2_minix_truncate(inode);
}

static struct super_block *minix_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, minix_fill_super);
}

static struct file_system_type minix_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "minix",
	.get_sb		= minix_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_minix_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&minix_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_minix_fs(void)
{
        unregister_filesystem(&minix_fs_type);
	destroy_inodecache();
}

module_init(init_minix_fs)
module_exit(exit_minix_fs)
MODULE_LICENSE("GPL");

