/*
 * ROMFS file system, Linux implementation
 *
 * Copyright (C) 1997-1999  Janos Farkas <chexum@shadow.banki.hu>
 *
 * Using parts of the minix filesystem
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * and parts of the affs filesystem additionally
 * Copyright (C) 1993  Ray Burr
 * Copyright (C) 1996  Hans-Joachim Widmaier
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
 */

/* todo:
 *	- see Documentation/filesystems/romfs.txt
 *	- use allocated, not stack memory for file names?
 *	- considering write access...
 *	- network (tftp) files?
 *	- merge back some _op tables
 */

/*
 * Sorry about some optimizations and for some goto's.  I just wanted
 * to squeeze some more bytes out of this code.. :)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/romfs_fs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>

#include <asm/uaccess.h>

struct romfs_inode_info {
	unsigned long i_metasize;	/* size of non-data area */
	unsigned long i_dataoffset;	/* from the start of fs */
	struct inode vfs_inode;
};

static struct inode *romfs_iget(struct super_block *, unsigned long);

/* instead of private superblock data */
static inline unsigned long romfs_maxsize(struct super_block *sb)
{
	return (unsigned long)sb->s_fs_info;
}

static inline struct romfs_inode_info *ROMFS_I(struct inode *inode)
{
	return container_of(inode, struct romfs_inode_info, vfs_inode);
}

static __u32
romfs_checksum(void *data, int size)
{
	__u32 sum;
	__be32 *ptr;

	sum = 0; ptr = data;
	size>>=2;
	while (size>0) {
		sum += be32_to_cpu(*ptr++);
		size--;
	}
	return sum;
}

static const struct super_operations romfs_ops;

static int romfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct romfs_super_block *rsb;
	struct inode *root;
	int sz, ret = -EINVAL;

	/* I would parse the options here, but there are none.. :) */

	sb_set_blocksize(s, ROMBSIZE);
	s->s_maxbytes = 0xFFFFFFFF;

	bh = sb_bread(s, 0);
	if (!bh) {
		/* XXX merge with other printk? */
                printk ("romfs: unable to read superblock\n");
		goto outnobh;
	}

	rsb = (struct romfs_super_block *)bh->b_data;
	sz = be32_to_cpu(rsb->size);
	if (rsb->word0 != ROMSB_WORD0 || rsb->word1 != ROMSB_WORD1
	   || sz < ROMFH_SIZE) {
		if (!silent)
			printk ("VFS: Can't find a romfs filesystem on dev "
				"%s.\n", s->s_id);
		goto out;
	}
	if (romfs_checksum(rsb, min_t(int, sz, 512))) {
		printk ("romfs: bad initial checksum on dev "
			"%s.\n", s->s_id);
		goto out;
	}

	s->s_magic = ROMFS_MAGIC;
	s->s_fs_info = (void *)(long)sz;

	s->s_flags |= MS_RDONLY;

	/* Find the start of the fs */
	sz = (ROMFH_SIZE +
	      strnlen(rsb->name, ROMFS_MAXFN) + 1 + ROMFH_PAD)
	     & ROMFH_MASK;

	s->s_op	= &romfs_ops;
	root = romfs_iget(s, sz);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}

	ret = -ENOMEM;
	s->s_root = d_alloc_root(root);
	if (!s->s_root)
		goto outiput;

	brelse(bh);
	return 0;

outiput:
	iput(root);
out:
	brelse(bh);
outnobh:
	return ret;
}

/* That's simple too. */

static int
romfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	buf->f_type = ROMFS_MAGIC;
	buf->f_bsize = ROMBSIZE;
	buf->f_bfree = buf->f_bavail = buf->f_ffree;
	buf->f_blocks = (romfs_maxsize(dentry->d_sb)+ROMBSIZE-1)>>ROMBSBITS;
	buf->f_namelen = ROMFS_MAXFN;
	return 0;
}

/* some helper routines */

static int
romfs_strnlen(struct inode *i, unsigned long offset, unsigned long count)
{
	struct buffer_head *bh;
	unsigned long avail, maxsize, res;

	maxsize = romfs_maxsize(i->i_sb);
	if (offset >= maxsize)
		return -1;

	/* strnlen is almost always valid */
	if (count > maxsize || offset+count > maxsize)
		count = maxsize-offset;

	bh = sb_bread(i->i_sb, offset>>ROMBSBITS);
	if (!bh)
		return -1;		/* error */

	avail = ROMBSIZE - (offset & ROMBMASK);
	maxsize = min_t(unsigned long, count, avail);
	res = strnlen(((char *)bh->b_data)+(offset&ROMBMASK), maxsize);
	brelse(bh);

	if (res < maxsize)
		return res;		/* found all of it */

	while (res < count) {
		offset += maxsize;

		bh = sb_bread(i->i_sb, offset>>ROMBSBITS);
		if (!bh)
			return -1;
		maxsize = min_t(unsigned long, count - res, ROMBSIZE);
		avail = strnlen(bh->b_data, maxsize);
		res += avail;
		brelse(bh);
		if (avail < maxsize)
			return res;
	}
	return res;
}

static int
romfs_copyfrom(struct inode *i, void *dest, unsigned long offset, unsigned long count)
{
	struct buffer_head *bh;
	unsigned long avail, maxsize, res;

	maxsize = romfs_maxsize(i->i_sb);
	if (offset >= maxsize || count > maxsize || offset+count>maxsize)
		return -1;

	bh = sb_bread(i->i_sb, offset>>ROMBSBITS);
	if (!bh)
		return -1;		/* error */

	avail = ROMBSIZE - (offset & ROMBMASK);
	maxsize = min_t(unsigned long, count, avail);
	memcpy(dest, ((char *)bh->b_data) + (offset & ROMBMASK), maxsize);
	brelse(bh);

	res = maxsize;			/* all of it */

	while (res < count) {
		offset += maxsize;
		dest += maxsize;

		bh = sb_bread(i->i_sb, offset>>ROMBSBITS);
		if (!bh)
			return -1;
		maxsize = min_t(unsigned long, count - res, ROMBSIZE);
		memcpy(dest, bh->b_data, maxsize);
		brelse(bh);
		res += maxsize;
	}
	return res;
}

static unsigned char romfs_dtype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_SOCK, DT_FIFO
};

static int
romfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *i = filp->f_path.dentry->d_inode;
	struct romfs_inode ri;
	unsigned long offset, maxoff;
	int j, ino, nextfh;
	int stored = 0;
	char fsname[ROMFS_MAXFN];	/* XXX dynamic? */

	lock_kernel();

	maxoff = romfs_maxsize(i->i_sb);

	offset = filp->f_pos;
	if (!offset) {
		offset = i->i_ino & ROMFH_MASK;
		if (romfs_copyfrom(i, &ri, offset, ROMFH_SIZE) <= 0)
			goto out;
		offset = be32_to_cpu(ri.spec) & ROMFH_MASK;
	}

	/* Not really failsafe, but we are read-only... */
	for(;;) {
		if (!offset || offset >= maxoff) {
			offset = maxoff;
			filp->f_pos = offset;
			goto out;
		}
		filp->f_pos = offset;

		/* Fetch inode info */
		if (romfs_copyfrom(i, &ri, offset, ROMFH_SIZE) <= 0)
			goto out;

		j = romfs_strnlen(i, offset+ROMFH_SIZE, sizeof(fsname)-1);
		if (j < 0)
			goto out;

		fsname[j]=0;
		romfs_copyfrom(i, fsname, offset+ROMFH_SIZE, j);

		ino = offset;
		nextfh = be32_to_cpu(ri.next);
		if ((nextfh & ROMFH_TYPE) == ROMFH_HRD)
			ino = be32_to_cpu(ri.spec);
		if (filldir(dirent, fsname, j, offset, ino,
			    romfs_dtype_table[nextfh & ROMFH_TYPE]) < 0) {
			goto out;
		}
		stored++;
		offset = nextfh & ROMFH_MASK;
	}
out:
	unlock_kernel();
	return stored;
}

static struct dentry *
romfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	unsigned long offset, maxoff;
	long res;
	int fslen;
	struct inode *inode = NULL;
	char fsname[ROMFS_MAXFN];	/* XXX dynamic? */
	struct romfs_inode ri;
	const char *name;		/* got from dentry */
	int len;

	res = -EACCES;			/* placeholder for "no data here" */
	offset = dir->i_ino & ROMFH_MASK;
	lock_kernel();
	if (romfs_copyfrom(dir, &ri, offset, ROMFH_SIZE) <= 0)
		goto error;

	maxoff = romfs_maxsize(dir->i_sb);
	offset = be32_to_cpu(ri.spec) & ROMFH_MASK;

	/* OK, now find the file whose name is in "dentry" in the
	 * directory specified by "dir".  */

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	for(;;) {
		if (!offset || offset >= maxoff)
			goto success; /* negative success */
		if (romfs_copyfrom(dir, &ri, offset, ROMFH_SIZE) <= 0)
			goto error;

		/* try to match the first 16 bytes of name */
		fslen = romfs_strnlen(dir, offset+ROMFH_SIZE, ROMFH_SIZE);
		if (len < ROMFH_SIZE) {
			if (len == fslen) {
				/* both are shorter, and same size */
				romfs_copyfrom(dir, fsname, offset+ROMFH_SIZE, len+1);
				if (strncmp (name, fsname, len) == 0)
					break;
			}
		} else if (fslen >= ROMFH_SIZE) {
			/* both are longer; XXX optimize max size */
			fslen = romfs_strnlen(dir, offset+ROMFH_SIZE, sizeof(fsname)-1);
			if (len == fslen) {
				romfs_copyfrom(dir, fsname, offset+ROMFH_SIZE, len+1);
				if (strncmp(name, fsname, len) == 0)
					break;
			}
		}
		/* next entry */
		offset = be32_to_cpu(ri.next) & ROMFH_MASK;
	}

	/* Hard link handling */
	if ((be32_to_cpu(ri.next) & ROMFH_TYPE) == ROMFH_HRD)
		offset = be32_to_cpu(ri.spec) & ROMFH_MASK;

	inode = romfs_iget(dir->i_sb, offset);
	if (IS_ERR(inode)) {
		res = PTR_ERR(inode);
		goto error;
	}

success:
	d_add(dentry, inode);
	res = 0;
error:
	unlock_kernel();
	return ERR_PTR(res);
}

/*
 * Ok, we do readpage, to be able to execute programs.  Unfortunately,
 * we can't use bmap, since we may have looser alignments.
 */

static int
romfs_readpage(struct file *file, struct page * page)
{
	struct inode *inode = page->mapping->host;
	loff_t offset, size;
	unsigned long filled;
	void *buf;
	int result = -EIO;

	page_cache_get(page);
	lock_kernel();
	buf = kmap(page);
	if (!buf)
		goto err_out;

	/* 32 bit warning -- but not for us :) */
	offset = page_offset(page);
	size = i_size_read(inode);
	filled = 0;
	result = 0;
	if (offset < size) {
		unsigned long readlen;

		size -= offset;
		readlen = size > PAGE_SIZE ? PAGE_SIZE : size;

		filled = romfs_copyfrom(inode, buf, ROMFS_I(inode)->i_dataoffset+offset, readlen);

		if (filled != readlen) {
			SetPageError(page);
			filled = 0;
			result = -EIO;
		}
	}

	if (filled < PAGE_SIZE)
		memset(buf + filled, 0, PAGE_SIZE-filled);

	if (!result)
		SetPageUptodate(page);
	flush_dcache_page(page);

	unlock_page(page);

	kunmap(page);
err_out:
	page_cache_release(page);
	unlock_kernel();

	return result;
}

/* Mapping from our types to the kernel */

static const struct address_space_operations romfs_aops = {
	.readpage = romfs_readpage
};

static const struct file_operations romfs_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= romfs_readdir,
};

static const struct inode_operations romfs_dir_inode_operations = {
	.lookup		= romfs_lookup,
};

static mode_t romfs_modemap[] =
{
	0, S_IFDIR+0644, S_IFREG+0644, S_IFLNK+0777,
	S_IFBLK+0600, S_IFCHR+0600, S_IFSOCK+0644, S_IFIFO+0644
};

static struct inode *
romfs_iget(struct super_block *sb, unsigned long ino)
{
	int nextfh, ret;
	struct romfs_inode ri;
	struct inode *i;

	ino &= ROMFH_MASK;
	i = iget_locked(sb, ino);
	if (!i)
		return ERR_PTR(-ENOMEM);
	if (!(i->i_state & I_NEW))
		return i;

	i->i_mode = 0;

	/* Loop for finding the real hard link */
	for(;;) {
		if (romfs_copyfrom(i, &ri, ino, ROMFH_SIZE) <= 0) {
			printk(KERN_ERR "romfs: read error for inode 0x%lx\n",
				ino);
			iget_failed(i);
			return ERR_PTR(-EIO);
		}
		/* XXX: do romfs_checksum here too (with name) */

		nextfh = be32_to_cpu(ri.next);
		if ((nextfh & ROMFH_TYPE) != ROMFH_HRD)
			break;

		ino = be32_to_cpu(ri.spec) & ROMFH_MASK;
	}

	i->i_nlink = 1;		/* Hard to decide.. */
	i->i_size = be32_to_cpu(ri.size);
	i->i_mtime.tv_sec = i->i_atime.tv_sec = i->i_ctime.tv_sec = 0;
	i->i_mtime.tv_nsec = i->i_atime.tv_nsec = i->i_ctime.tv_nsec = 0;

        /* Precalculate the data offset */
	ret = romfs_strnlen(i, ino + ROMFH_SIZE, ROMFS_MAXFN);
	if (ret >= 0)
		ino = (ROMFH_SIZE + ret + 1 + ROMFH_PAD) & ROMFH_MASK;
	else
		ino = 0;

        ROMFS_I(i)->i_metasize = ino;
        ROMFS_I(i)->i_dataoffset = ino+(i->i_ino&ROMFH_MASK);

        /* Compute permissions */
        ino = romfs_modemap[nextfh & ROMFH_TYPE];
	/* only "normal" files have ops */
	switch (nextfh & ROMFH_TYPE) {
		case 1:
			i->i_size = ROMFS_I(i)->i_metasize;
			i->i_op = &romfs_dir_inode_operations;
			i->i_fop = &romfs_dir_operations;
			if (nextfh & ROMFH_EXEC)
				ino |= S_IXUGO;
			i->i_mode = ino;
			break;
		case 2:
			i->i_fop = &generic_ro_fops;
			i->i_data.a_ops = &romfs_aops;
			if (nextfh & ROMFH_EXEC)
				ino |= S_IXUGO;
			i->i_mode = ino;
			break;
		case 3:
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &romfs_aops;
			i->i_mode = ino | S_IRWXUGO;
			break;
		default:
			/* depending on MBZ for sock/fifos */
			nextfh = be32_to_cpu(ri.spec);
			init_special_inode(i, ino,
					MKDEV(nextfh>>16,nextfh&0xffff));
	}
	unlock_new_inode(i);
	return i;
}

static struct kmem_cache * romfs_inode_cachep;

static struct inode *romfs_alloc_inode(struct super_block *sb)
{
	struct romfs_inode_info *ei;
	ei = kmem_cache_alloc(romfs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void romfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(romfs_inode_cachep, ROMFS_I(inode));
}

static void init_once(void *foo)
{
	struct romfs_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	romfs_inode_cachep = kmem_cache_create("romfs_inode_cache",
					     sizeof(struct romfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (romfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(romfs_inode_cachep);
}

static int romfs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_RDONLY;
	return 0;
}

static const struct super_operations romfs_ops = {
	.alloc_inode	= romfs_alloc_inode,
	.destroy_inode	= romfs_destroy_inode,
	.statfs		= romfs_statfs,
	.remount_fs	= romfs_remount,
};

static int romfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, romfs_fill_super,
			   mnt);
}

static struct file_system_type romfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "romfs",
	.get_sb		= romfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_romfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
        err = register_filesystem(&romfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_romfs_fs(void)
{
	unregister_filesystem(&romfs_fs_type);
	destroy_inodecache();
}

/* Yes, works even as a module... :) */

module_init(init_romfs_fs)
module_exit(exit_romfs_fs)
MODULE_LICENSE("GPL");
