/*
 * Optimized MPEG FS - inode and super operations.
 * Copyright (C) 2006 Bob Copeland <me@bobcopeland.com>
 * Released under GPL v2.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/parser.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#include <linux/crc-itu-t.h>
#include "omfs.h"

MODULE_AUTHOR("Bob Copeland <me@bobcopeland.com>");
MODULE_DESCRIPTION("OMFS (ReplayTV/Karma) Filesystem for Linux");
MODULE_LICENSE("GPL");

struct inode *omfs_new_inode(struct inode *dir, int mode)
{
	struct inode *inode;
	u64 new_block;
	int err;
	int len;
	struct omfs_sb_info *sbi = OMFS_SB(dir->i_sb);

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	err = omfs_allocate_range(dir->i_sb, sbi->s_mirrors, sbi->s_mirrors,
			&new_block, &len);
	if (err)
		goto fail;

	inode->i_ino = new_block;
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_mapping->a_ops = &omfs_aops;

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	case S_IFDIR:
		inode->i_op = &omfs_dir_inops;
		inode->i_fop = &omfs_dir_operations;
		inode->i_size = sbi->s_sys_blocksize;
		inc_nlink(inode);
		break;
	case S_IFREG:
		inode->i_op = &omfs_file_inops;
		inode->i_fop = &omfs_file_operations;
		inode->i_size = 0;
		break;
	}

	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	return inode;
fail:
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(err);
}

/*
 * Update the header checksums for a dirty inode based on its contents.
 * Caller is expected to hold the buffer head underlying oi and mark it
 * dirty.
 */
static void omfs_update_checksums(struct omfs_inode *oi)
{
	int xor, i, ofs = 0, count;
	u16 crc = 0;
	unsigned char *ptr = (unsigned char *) oi;

	count = be32_to_cpu(oi->i_head.h_body_size);
	ofs = sizeof(struct omfs_header);

	crc = crc_itu_t(crc, ptr + ofs, count);
	oi->i_head.h_crc = cpu_to_be16(crc);

	xor = ptr[0];
	for (i = 1; i < OMFS_XOR_COUNT; i++)
		xor ^= ptr[i];

	oi->i_head.h_check_xor = xor;
}

static int omfs_write_inode(struct inode *inode, int wait)
{
	struct omfs_inode *oi;
	struct omfs_sb_info *sbi = OMFS_SB(inode->i_sb);
	struct buffer_head *bh, *bh2;
	unsigned int block;
	u64 ctime;
	int i;
	int ret = -EIO;
	int sync_failed = 0;

	/* get current inode since we may have written sibling ptrs etc. */
	block = clus_to_blk(sbi, inode->i_ino);
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto out;

	oi = (struct omfs_inode *) bh->b_data;

	oi->i_head.h_self = cpu_to_be64(inode->i_ino);
	if (S_ISDIR(inode->i_mode))
		oi->i_type = OMFS_DIR;
	else if (S_ISREG(inode->i_mode))
		oi->i_type = OMFS_FILE;
	else {
		printk(KERN_WARNING "omfs: unknown file type: %d\n",
			inode->i_mode);
		goto out_brelse;
	}

	oi->i_head.h_body_size = cpu_to_be32(sbi->s_sys_blocksize -
		sizeof(struct omfs_header));
	oi->i_head.h_version = 1;
	oi->i_head.h_type = OMFS_INODE_NORMAL;
	oi->i_head.h_magic = OMFS_IMAGIC;
	oi->i_size = cpu_to_be64(inode->i_size);

	ctime = inode->i_ctime.tv_sec * 1000LL +
		((inode->i_ctime.tv_nsec + 999)/1000);
	oi->i_ctime = cpu_to_be64(ctime);

	omfs_update_checksums(oi);

	mark_buffer_dirty(bh);
	if (wait) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			sync_failed = 1;
	}

	/* if mirroring writes, copy to next fsblock */
	for (i = 1; i < sbi->s_mirrors; i++) {
		bh2 = sb_bread(inode->i_sb, block + i *
			(sbi->s_blocksize / sbi->s_sys_blocksize));
		if (!bh2)
			goto out_brelse;

		memcpy(bh2->b_data, bh->b_data, bh->b_size);
		mark_buffer_dirty(bh2);
		if (wait) {
			sync_dirty_buffer(bh2);
			if (buffer_req(bh2) && !buffer_uptodate(bh2))
				sync_failed = 1;
		}
		brelse(bh2);
	}
	ret = (sync_failed) ? -EIO : 0;
out_brelse:
	brelse(bh);
out:
	return ret;
}

int omfs_sync_inode(struct inode *inode)
{
	return omfs_write_inode(inode, 1);
}

/*
 * called when an entry is deleted, need to clear the bits in the
 * bitmaps.
 */
static void omfs_delete_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);

	if (S_ISREG(inode->i_mode)) {
		inode->i_size = 0;
		omfs_shrink_inode(inode);
	}

	omfs_clear_range(inode->i_sb, inode->i_ino, 2);
	clear_inode(inode);
}

struct inode *omfs_iget(struct super_block *sb, ino_t ino)
{
	struct omfs_sb_info *sbi = OMFS_SB(sb);
	struct omfs_inode *oi;
	struct buffer_head *bh;
	unsigned int block;
	u64 ctime;
	unsigned long nsecs;
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	block = clus_to_blk(sbi, ino);
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto iget_failed;

	oi = (struct omfs_inode *)bh->b_data;

	/* check self */
	if (ino != be64_to_cpu(oi->i_head.h_self))
		goto fail_bh;

	inode->i_uid = sbi->s_uid;
	inode->i_gid = sbi->s_gid;

	ctime = be64_to_cpu(oi->i_ctime);
	nsecs = do_div(ctime, 1000) * 1000L;

	inode->i_atime.tv_sec = ctime;
	inode->i_mtime.tv_sec = ctime;
	inode->i_ctime.tv_sec = ctime;
	inode->i_atime.tv_nsec = nsecs;
	inode->i_mtime.tv_nsec = nsecs;
	inode->i_ctime.tv_nsec = nsecs;

	inode->i_mapping->a_ops = &omfs_aops;

	switch (oi->i_type) {
	case OMFS_DIR:
		inode->i_mode = S_IFDIR | (S_IRWXUGO & ~sbi->s_dmask);
		inode->i_op = &omfs_dir_inops;
		inode->i_fop = &omfs_dir_operations;
		inode->i_size = sbi->s_sys_blocksize;
		inc_nlink(inode);
		break;
	case OMFS_FILE:
		inode->i_mode = S_IFREG | (S_IRWXUGO & ~sbi->s_fmask);
		inode->i_fop = &omfs_file_operations;
		inode->i_size = be64_to_cpu(oi->i_size);
		break;
	}
	brelse(bh);
	unlock_new_inode(inode);
	return inode;
fail_bh:
	brelse(bh);
iget_failed:
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

static void omfs_put_super(struct super_block *sb)
{
	struct omfs_sb_info *sbi = OMFS_SB(sb);
	kfree(sbi->s_imap);
	kfree(sbi);
	sb->s_fs_info = NULL;
}

static int omfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *s = dentry->d_sb;
	struct omfs_sb_info *sbi = OMFS_SB(s);
	buf->f_type = OMFS_MAGIC;
	buf->f_bsize = sbi->s_blocksize;
	buf->f_blocks = sbi->s_num_blocks;
	buf->f_files = sbi->s_num_blocks;
	buf->f_namelen = OMFS_NAMELEN;

	buf->f_bfree = buf->f_bavail = buf->f_ffree =
		omfs_count_free(s);
	return 0;
}

static struct super_operations omfs_sops = {
	.write_inode	= omfs_write_inode,
	.delete_inode	= omfs_delete_inode,
	.put_super	= omfs_put_super,
	.statfs		= omfs_statfs,
	.show_options	= generic_show_options,
};

/*
 * For Rio Karma, there is an on-disk free bitmap whose location is
 * stored in the root block.  For ReplayTV, there is no such free bitmap
 * so we have to walk the tree.  Both inodes and file data are allocated
 * from the same map.  This array can be big (300k) so we allocate
 * in units of the blocksize.
 */
static int omfs_get_imap(struct super_block *sb)
{
	int bitmap_size;
	int array_size;
	int count;
	struct omfs_sb_info *sbi = OMFS_SB(sb);
	struct buffer_head *bh;
	unsigned long **ptr;
	sector_t block;

	bitmap_size = DIV_ROUND_UP(sbi->s_num_blocks, 8);
	array_size = DIV_ROUND_UP(bitmap_size, sb->s_blocksize);

	if (sbi->s_bitmap_ino == ~0ULL)
		goto out;

	sbi->s_imap_size = array_size;
	sbi->s_imap = kzalloc(array_size * sizeof(unsigned long *), GFP_KERNEL);
	if (!sbi->s_imap)
		goto nomem;

	block = clus_to_blk(sbi, sbi->s_bitmap_ino);
	ptr = sbi->s_imap;
	for (count = bitmap_size; count > 0; count -= sb->s_blocksize) {
		bh = sb_bread(sb, block++);
		if (!bh)
			goto nomem_free;
		*ptr = kmalloc(sb->s_blocksize, GFP_KERNEL);
		if (!*ptr) {
			brelse(bh);
			goto nomem_free;
		}
		memcpy(*ptr, bh->b_data, sb->s_blocksize);
		if (count < sb->s_blocksize)
			memset((void *)*ptr + count, 0xff,
				sb->s_blocksize - count);
		brelse(bh);
		ptr++;
	}
out:
	return 0;

nomem_free:
	for (count = 0; count < array_size; count++)
		kfree(sbi->s_imap[count]);

	kfree(sbi->s_imap);
nomem:
	sbi->s_imap = NULL;
	sbi->s_imap_size = 0;
	return -ENOMEM;
}

enum {
	Opt_uid, Opt_gid, Opt_umask, Opt_dmask, Opt_fmask
};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_dmask, "dmask=%o"},
	{Opt_fmask, "fmask=%o"},
};

static int parse_options(char *options, struct omfs_sb_info *sbi)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
			sbi->s_uid = option;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			sbi->s_gid = option;
			break;
		case Opt_umask:
			if (match_octal(&args[0], &option))
				return 0;
			sbi->s_fmask = sbi->s_dmask = option;
			break;
		case Opt_dmask:
			if (match_octal(&args[0], &option))
				return 0;
			sbi->s_dmask = option;
			break;
		case Opt_fmask:
			if (match_octal(&args[0], &option))
				return 0;
			sbi->s_fmask = option;
			break;
		default:
			return 0;
		}
	}
	return 1;
}

static int omfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh, *bh2;
	struct omfs_super_block *omfs_sb;
	struct omfs_root_block *omfs_rb;
	struct omfs_sb_info *sbi;
	struct inode *root;
	sector_t start;
	int ret = -EINVAL;

	save_mount_options(sb, (char *) data);

	sbi = kzalloc(sizeof(struct omfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;

	sbi->s_uid = current_uid();
	sbi->s_gid = current_gid();
	sbi->s_dmask = sbi->s_fmask = current->fs->umask;

	if (!parse_options((char *) data, sbi))
		goto end;

	sb->s_maxbytes = 0xffffffff;

	sb_set_blocksize(sb, 0x200);

	bh = sb_bread(sb, 0);
	if (!bh)
		goto end;

	omfs_sb = (struct omfs_super_block *)bh->b_data;

	if (omfs_sb->s_magic != cpu_to_be32(OMFS_MAGIC)) {
		if (!silent)
			printk(KERN_ERR "omfs: Invalid superblock (%x)\n",
				   omfs_sb->s_magic);
		goto out_brelse_bh;
	}
	sb->s_magic = OMFS_MAGIC;

	sbi->s_num_blocks = be64_to_cpu(omfs_sb->s_num_blocks);
	sbi->s_blocksize = be32_to_cpu(omfs_sb->s_blocksize);
	sbi->s_mirrors = be32_to_cpu(omfs_sb->s_mirrors);
	sbi->s_root_ino = be64_to_cpu(omfs_sb->s_root_block);
	sbi->s_sys_blocksize = be32_to_cpu(omfs_sb->s_sys_blocksize);
	mutex_init(&sbi->s_bitmap_lock);

	if (sbi->s_sys_blocksize > PAGE_SIZE) {
		printk(KERN_ERR "omfs: sysblock size (%d) is out of range\n",
			sbi->s_sys_blocksize);
		goto out_brelse_bh;
	}

	if (sbi->s_blocksize < sbi->s_sys_blocksize ||
	    sbi->s_blocksize > OMFS_MAX_BLOCK_SIZE) {
		printk(KERN_ERR "omfs: block size (%d) is out of range\n",
			sbi->s_blocksize);
		goto out_brelse_bh;
	}

	/*
	 * Use sys_blocksize as the fs block since it is smaller than a
	 * page while the fs blocksize can be larger.
	 */
	sb_set_blocksize(sb, sbi->s_sys_blocksize);

	/*
	 * ...and the difference goes into a shift.  sys_blocksize is always
	 * a power of two factor of blocksize.
	 */
	sbi->s_block_shift = get_bitmask_order(sbi->s_blocksize) -
		get_bitmask_order(sbi->s_sys_blocksize);

	start = clus_to_blk(sbi, be64_to_cpu(omfs_sb->s_root_block));
	bh2 = sb_bread(sb, start);
	if (!bh2)
		goto out_brelse_bh;

	omfs_rb = (struct omfs_root_block *)bh2->b_data;

	sbi->s_bitmap_ino = be64_to_cpu(omfs_rb->r_bitmap);
	sbi->s_clustersize = be32_to_cpu(omfs_rb->r_clustersize);

	if (sbi->s_num_blocks != be64_to_cpu(omfs_rb->r_num_blocks)) {
		printk(KERN_ERR "omfs: block count discrepancy between "
			"super and root blocks (%llx, %llx)\n",
			(unsigned long long)sbi->s_num_blocks,
			(unsigned long long)be64_to_cpu(omfs_rb->r_num_blocks));
		goto out_brelse_bh2;
	}

	ret = omfs_get_imap(sb);
	if (ret)
		goto out_brelse_bh2;

	sb->s_op = &omfs_sops;

	root = omfs_iget(sb, be64_to_cpu(omfs_rb->r_root_dir));
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out_brelse_bh2;
	}

	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		goto out_brelse_bh2;
	}
	printk(KERN_DEBUG "omfs: Mounted volume %s\n", omfs_rb->r_name);

	ret = 0;
out_brelse_bh2:
	brelse(bh2);
out_brelse_bh:
	brelse(bh);
end:
	return ret;
}

static int omfs_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *m)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, omfs_fill_super, m);
}

static struct file_system_type omfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "omfs",
	.get_sb = omfs_get_sb,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static int __init init_omfs_fs(void)
{
	return register_filesystem(&omfs_fs_type);
}

static void __exit exit_omfs_fs(void)
{
	unregister_filesystem(&omfs_fs_type);
}

module_init(init_omfs_fs);
module_exit(exit_omfs_fs);
