/* Block- or MTD-based romfs
 *
 * Copyright © 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Derived from: ROMFS file system, Linux implementation
 *
 * Copyright © 1997-1999  Janos Farkas <chexum@shadow.banki.hu>
 *
 * Using parts of the minix filesystem
 * Copyright © 1991, 1992  Linus Torvalds
 *
 * and parts of the affs filesystem additionally
 * Copyright © 1993  Ray Burr
 * Copyright © 1996  Hans-Joachim Widmaier
 *
 * Changes
 *					Changed for 2.1.19 modules
 *	Jan 1997			Initial release
 *	Jun 1997			2.1.43+ changes
 *					Proper page locking in readpage
 *					Changed to work with 2.1.45+ fs
 *	Jul 1997			Fixed follow_link
 *			2.1.47
 *					lookup shouldn't return -ENOENT
 *					from Horst von Brand:
 *					  fail on wrong checksum
 *					  double unlock_super was possible
 *					  correct namelen for statfs
 *					spotted by Bill Hawes:
 *					  readlink shouldn't iput()
 *	Jun 1998	2.1.106		from Avery Pennarun: glibc scandir()
 *					  exposed a problem in readdir
 *			2.1.107		code-freeze spellchecker run
 *	Aug 1998			2.1.118+ VFS changes
 *	Sep 1998	2.1.122		another VFS change (follow_link)
 *	Apr 1999	2.2.7		no more EBADF checking in
 *					  lookup/readdir, use ERR_PTR
 *	Jun 1999	2.3.6		d_alloc_root use changed
 *			2.3.9		clean up usage of ENOENT/negative
 *					  dentries in lookup
 *					clean up page flags setting
 *					  (error, uptodate, locking) in
 *					  in readpage
 *					use init_special_inode for
 *					  fifos/sockets (and streamline) in
 *					  read_inode, fix _ops table order
 *	Aug 1999	2.3.16		__initfunc() => __init change
 *	Oct 1999	2.3.24		page->owner hack obsoleted
 *	Nov 1999	2.3.27		2.3.25+ page->offset => index change
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/mtd/super.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uaccess.h>
#include "internal.h"

static struct kmem_cache *romfs_inode_cachep;

static const umode_t romfs_modemap[8] = {
	0,			/* hard link */
	S_IFDIR  | 0644,	/* directory */
	S_IFREG  | 0644,	/* regular file */
	S_IFLNK  | 0777,	/* symlink */
	S_IFBLK  | 0600,	/* blockdev */
	S_IFCHR  | 0600,	/* chardev */
	S_IFSOCK | 0644,	/* socket */
	S_IFIFO  | 0644		/* FIFO */
};

static const unsigned char romfs_dtype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_SOCK, DT_FIFO
};

static struct inode *romfs_iget(struct super_block *sb, unsigned long pos);

/*
 * read a page worth of data from the image
 */
static int romfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	loff_t offset, size;
	unsigned long fillsize, pos;
	void *buf;
	int ret;

	buf = kmap(page);
	if (!buf)
		return -ENOMEM;

	/* 32 bit warning -- but not for us :) */
	offset = page_offset(page);
	size = i_size_read(inode);
	fillsize = 0;
	ret = 0;
	if (offset < size) {
		size -= offset;
		fillsize = size > PAGE_SIZE ? PAGE_SIZE : size;

		pos = ROMFS_I(inode)->i_dataoffset + offset;

		ret = romfs_dev_read(inode->i_sb, pos, buf, fillsize);
		if (ret < 0) {
			SetPageError(page);
			fillsize = 0;
			ret = -EIO;
		}
	}

	if (fillsize < PAGE_SIZE)
		memset(buf + fillsize, 0, PAGE_SIZE - fillsize);
	if (ret == 0)
		SetPageUptodate(page);

	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);
	return ret;
}

static const struct address_space_operations romfs_aops = {
	.readpage	= romfs_readpage
};

/*
 * read the entries from a directory
 */
static int romfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *i = file_inode(file);
	struct romfs_inode ri;
	unsigned long offset, maxoff;
	int j, ino, nextfh;
	char fsname[ROMFS_MAXFN];	/* XXX dynamic? */
	int ret;

	maxoff = romfs_maxsize(i->i_sb);

	offset = ctx->pos;
	if (!offset) {
		offset = i->i_ino & ROMFH_MASK;
		ret = romfs_dev_read(i->i_sb, offset, &ri, ROMFH_SIZE);
		if (ret < 0)
			goto out;
		offset = be32_to_cpu(ri.spec) & ROMFH_MASK;
	}

	/* Not really failsafe, but we are read-only... */
	for (;;) {
		if (!offset || offset >= maxoff) {
			offset = maxoff;
			ctx->pos = offset;
			goto out;
		}
		ctx->pos = offset;

		/* Fetch inode info */
		ret = romfs_dev_read(i->i_sb, offset, &ri, ROMFH_SIZE);
		if (ret < 0)
			goto out;

		j = romfs_dev_strnlen(i->i_sb, offset + ROMFH_SIZE,
				      sizeof(fsname) - 1);
		if (j < 0)
			goto out;

		ret = romfs_dev_read(i->i_sb, offset + ROMFH_SIZE, fsname, j);
		if (ret < 0)
			goto out;
		fsname[j] = '\0';

		ino = offset;
		nextfh = be32_to_cpu(ri.next);
		if ((nextfh & ROMFH_TYPE) == ROMFH_HRD)
			ino = be32_to_cpu(ri.spec);
		if (!dir_emit(ctx, fsname, j, ino,
			    romfs_dtype_table[nextfh & ROMFH_TYPE]))
			goto out;

		offset = nextfh & ROMFH_MASK;
	}
out:
	return 0;
}

/*
 * look up an entry in a directory
 */
static struct dentry *romfs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	unsigned long offset, maxoff;
	struct inode *inode;
	struct romfs_inode ri;
	const char *name;		/* got from dentry */
	int len, ret;

	offset = dir->i_ino & ROMFH_MASK;
	ret = romfs_dev_read(dir->i_sb, offset, &ri, ROMFH_SIZE);
	if (ret < 0)
		goto error;

	/* search all the file entries in the list starting from the one
	 * pointed to by the directory's special data */
	maxoff = romfs_maxsize(dir->i_sb);
	offset = be32_to_cpu(ri.spec) & ROMFH_MASK;

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	for (;;) {
		if (!offset || offset >= maxoff)
			goto out0;

		ret = romfs_dev_read(dir->i_sb, offset, &ri, sizeof(ri));
		if (ret < 0)
			goto error;

		/* try to match the first 16 bytes of name */
		ret = romfs_dev_strcmp(dir->i_sb, offset + ROMFH_SIZE, name,
				       len);
		if (ret < 0)
			goto error;
		if (ret == 1)
			break;

		/* next entry */
		offset = be32_to_cpu(ri.next) & ROMFH_MASK;
	}

	/* Hard link handling */
	if ((be32_to_cpu(ri.next) & ROMFH_TYPE) == ROMFH_HRD)
		offset = be32_to_cpu(ri.spec) & ROMFH_MASK;

	inode = romfs_iget(dir->i_sb, offset);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto error;
	}
	goto outi;

	/*
	 * it's a bit funky, _lookup needs to return an error code
	 * (negative) or a NULL, both as a dentry.  ENOENT should not
	 * be returned, instead we need to create a negative dentry by
	 * d_add(dentry, NULL); and return 0 as no error.
	 * (Although as I see, it only matters on writable file
	 * systems).
	 */
out0:
	inode = NULL;
outi:
	d_add(dentry, inode);
	ret = 0;
error:
	return ERR_PTR(ret);
}

static const struct file_operations romfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate	= romfs_readdir,
	.llseek		= default_llseek,
};

static const struct inode_operations romfs_dir_inode_operations = {
	.lookup		= romfs_lookup,
};

/*
 * get a romfs inode based on its position in the image (which doubles as the
 * inode number)
 */
static struct inode *romfs_iget(struct super_block *sb, unsigned long pos)
{
	struct romfs_inode_info *inode;
	struct romfs_inode ri;
	struct inode *i;
	unsigned long nlen;
	unsigned nextfh;
	int ret;
	umode_t mode;

	/* we might have to traverse a chain of "hard link" file entries to get
	 * to the actual file */
	for (;;) {
		ret = romfs_dev_read(sb, pos, &ri, sizeof(ri));
		if (ret < 0)
			goto error;

		/* XXX: do romfs_checksum here too (with name) */

		nextfh = be32_to_cpu(ri.next);
		if ((nextfh & ROMFH_TYPE) != ROMFH_HRD)
			break;

		pos = be32_to_cpu(ri.spec) & ROMFH_MASK;
	}

	/* determine the length of the filename */
	nlen = romfs_dev_strnlen(sb, pos + ROMFH_SIZE, ROMFS_MAXFN);
	if (IS_ERR_VALUE(nlen))
		goto eio;

	/* get an inode for this image position */
	i = iget_locked(sb, pos);
	if (!i)
		return ERR_PTR(-ENOMEM);

	if (!(i->i_state & I_NEW))
		return i;

	/* precalculate the data offset */
	inode = ROMFS_I(i);
	inode->i_metasize = (ROMFH_SIZE + nlen + 1 + ROMFH_PAD) & ROMFH_MASK;
	inode->i_dataoffset = pos + inode->i_metasize;

	set_nlink(i, 1);		/* Hard to decide.. */
	i->i_size = be32_to_cpu(ri.size);
	i->i_mtime.tv_sec = i->i_atime.tv_sec = i->i_ctime.tv_sec = 0;
	i->i_mtime.tv_nsec = i->i_atime.tv_nsec = i->i_ctime.tv_nsec = 0;

	/* set up mode and ops */
	mode = romfs_modemap[nextfh & ROMFH_TYPE];

	switch (nextfh & ROMFH_TYPE) {
	case ROMFH_DIR:
		i->i_size = ROMFS_I(i)->i_metasize;
		i->i_op = &romfs_dir_inode_operations;
		i->i_fop = &romfs_dir_operations;
		if (nextfh & ROMFH_EXEC)
			mode |= S_IXUGO;
		break;
	case ROMFH_REG:
		i->i_fop = &romfs_ro_fops;
		i->i_data.a_ops = &romfs_aops;
		if (i->i_sb->s_mtd)
			i->i_data.backing_dev_info =
				i->i_sb->s_mtd->backing_dev_info;
		if (nextfh & ROMFH_EXEC)
			mode |= S_IXUGO;
		break;
	case ROMFH_SYM:
		i->i_op = &page_symlink_inode_operations;
		i->i_data.a_ops = &romfs_aops;
		mode |= S_IRWXUGO;
		break;
	default:
		/* depending on MBZ for sock/fifos */
		nextfh = be32_to_cpu(ri.spec);
		init_special_inode(i, mode, MKDEV(nextfh >> 16,
						  nextfh & 0xffff));
		break;
	}

	i->i_mode = mode;

	unlock_new_inode(i);
	return i;

eio:
	ret = -EIO;
error:
	pr_err("ROMFS: read error for inode 0x%lx\n", pos);
	return ERR_PTR(ret);
}

/*
 * allocate a new inode
 */
static struct inode *romfs_alloc_inode(struct super_block *sb)
{
	struct romfs_inode_info *inode;
	inode = kmem_cache_alloc(romfs_inode_cachep, GFP_KERNEL);
	return inode ? &inode->vfs_inode : NULL;
}

/*
 * return a spent inode to the slab cache
 */
static void romfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(romfs_inode_cachep, ROMFS_I(inode));
}

static void romfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, romfs_i_callback);
}

/*
 * get filesystem statistics
 */
static int romfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = ROMFS_MAGIC;
	buf->f_namelen = ROMFS_MAXFN;
	buf->f_bsize = ROMBSIZE;
	buf->f_bfree = buf->f_bavail = buf->f_ffree;
	buf->f_blocks =
		(romfs_maxsize(dentry->d_sb) + ROMBSIZE - 1) >> ROMBSBITS;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	return 0;
}

/*
 * remounting must involve read-only
 */
static int romfs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	*flags |= MS_RDONLY;
	return 0;
}

static const struct super_operations romfs_super_ops = {
	.alloc_inode	= romfs_alloc_inode,
	.destroy_inode	= romfs_destroy_inode,
	.statfs		= romfs_statfs,
	.remount_fs	= romfs_remount,
};

/*
 * checksum check on part of a romfs filesystem
 */
static __u32 romfs_checksum(const void *data, int size)
{
	const __be32 *ptr = data;
	__u32 sum;

	sum = 0;
	size >>= 2;
	while (size > 0) {
		sum += be32_to_cpu(*ptr++);
		size--;
	}
	return sum;
}

/*
 * fill in the superblock
 */
static int romfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct romfs_super_block *rsb;
	struct inode *root;
	unsigned long pos, img_size;
	const char *storage;
	size_t len;
	int ret;

#ifdef CONFIG_BLOCK
	if (!sb->s_mtd) {
		sb_set_blocksize(sb, ROMBSIZE);
	} else {
		sb->s_blocksize = ROMBSIZE;
		sb->s_blocksize_bits = blksize_bits(ROMBSIZE);
	}
#endif

	sb->s_maxbytes = 0xFFFFFFFF;
	sb->s_magic = ROMFS_MAGIC;
	sb->s_flags |= MS_RDONLY | MS_NOATIME;
	sb->s_op = &romfs_super_ops;

	/* read the image superblock and check it */
	rsb = kmalloc(512, GFP_KERNEL);
	if (!rsb)
		return -ENOMEM;

	sb->s_fs_info = (void *) 512;
	ret = romfs_dev_read(sb, 0, rsb, 512);
	if (ret < 0)
		goto error_rsb;

	img_size = be32_to_cpu(rsb->size);

	if (sb->s_mtd && img_size > sb->s_mtd->size)
		goto error_rsb_inval;

	sb->s_fs_info = (void *) img_size;

	if (rsb->word0 != ROMSB_WORD0 || rsb->word1 != ROMSB_WORD1 ||
	    img_size < ROMFH_SIZE) {
		if (!silent)
			pr_warn("VFS: Can't find a romfs filesystem on dev %s.\n",
			       sb->s_id);
		goto error_rsb_inval;
	}

	if (romfs_checksum(rsb, min_t(size_t, img_size, 512))) {
		pr_err("ROMFS: bad initial checksum on dev %s.\n",
		       sb->s_id);
		goto error_rsb_inval;
	}

	storage = sb->s_mtd ? "MTD" : "the block layer";

	len = strnlen(rsb->name, ROMFS_MAXFN);
	if (!silent)
		pr_notice("ROMFS: Mounting image '%*.*s' through %s\n",
		       (unsigned) len, (unsigned) len, rsb->name, storage);

	kfree(rsb);
	rsb = NULL;

	/* find the root directory */
	pos = (ROMFH_SIZE + len + 1 + ROMFH_PAD) & ROMFH_MASK;

	root = romfs_iget(sb, pos);
	if (IS_ERR(root))
		return PTR_ERR(root);

	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;

error_rsb_inval:
	ret = -EINVAL;
error_rsb:
	kfree(rsb);
	return ret;
}

/*
 * get a superblock for mounting
 */
static struct dentry *romfs_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	struct dentry *ret = ERR_PTR(-EINVAL);

#ifdef CONFIG_ROMFS_ON_MTD
	ret = mount_mtd(fs_type, flags, dev_name, data, romfs_fill_super);
#endif
#ifdef CONFIG_ROMFS_ON_BLOCK
	if (ret == ERR_PTR(-EINVAL))
		ret = mount_bdev(fs_type, flags, dev_name, data,
				  romfs_fill_super);
#endif
	return ret;
}

/*
 * destroy a romfs superblock in the appropriate manner
 */
static void romfs_kill_sb(struct super_block *sb)
{
#ifdef CONFIG_ROMFS_ON_MTD
	if (sb->s_mtd) {
		kill_mtd_super(sb);
		return;
	}
#endif
#ifdef CONFIG_ROMFS_ON_BLOCK
	if (sb->s_bdev) {
		kill_block_super(sb);
		return;
	}
#endif
}

static struct file_system_type romfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "romfs",
	.mount		= romfs_mount,
	.kill_sb	= romfs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("romfs");

/*
 * inode storage initialiser
 */
static void romfs_i_init_once(void *_inode)
{
	struct romfs_inode_info *inode = _inode;

	inode_init_once(&inode->vfs_inode);
}

/*
 * romfs module initialisation
 */
static int __init init_romfs_fs(void)
{
	int ret;

	pr_info("ROMFS MTD (C) 2007 Red Hat, Inc.\n");

	romfs_inode_cachep =
		kmem_cache_create("romfs_i",
				  sizeof(struct romfs_inode_info), 0,
				  SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
				  romfs_i_init_once);

	if (!romfs_inode_cachep) {
		pr_err("ROMFS error: Failed to initialise inode cache\n");
		return -ENOMEM;
	}
	ret = register_filesystem(&romfs_fs_type);
	if (ret) {
		pr_err("ROMFS error: Failed to register filesystem\n");
		goto error_register;
	}
	return 0;

error_register:
	kmem_cache_destroy(romfs_inode_cachep);
	return ret;
}

/*
 * romfs module removal
 */
static void __exit exit_romfs_fs(void)
{
	unregister_filesystem(&romfs_fs_type);
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(romfs_inode_cachep);
}

module_init(init_romfs_fs);
module_exit(exit_romfs_fs);

MODULE_DESCRIPTION("Direct-MTD Capable RomFS");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL"); /* Actually dual-licensed, but it doesn't matter for */
