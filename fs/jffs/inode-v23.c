/*
 * JFFS -- Journalling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications AB.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: inode-v23.c,v 1.70 2001/10/02 09:16:02 dwmw2 Exp $
 *
 * Ported to Linux 2.3.x and MTD:
 * Copyright (C) 2000  Alexander Larsson (alex@cendio.se), Cendio Systems AB
 *
 * Copyright 2000, 2001  Red Hat, Inc.
 */

/* inode.c -- Contains the code that is called from the VFS.  */

/* TODO-ALEX:
 * uid and gid are just 16 bit.
 * jffs_file_write reads from user-space pointers without xx_from_user
 * maybe other stuff do to.
 */

#include <linux/time.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/jffs.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/stat.h>
#include <linux/blkdev.h>
#include <linux/quotaops.h>
#include <linux/highmem.h>
#include <linux/vfs.h>
#include <linux/mutex.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include "jffs_fm.h"
#include "intrep.h"
#ifdef CONFIG_JFFS_PROC_FS
#include "jffs_proc.h"
#endif

static int jffs_remove(struct inode *dir, struct dentry *dentry, int type);

static struct super_operations jffs_ops;
static const struct file_operations jffs_file_operations;
static struct inode_operations jffs_file_inode_operations;
static const struct file_operations jffs_dir_operations;
static struct inode_operations jffs_dir_inode_operations;
static struct address_space_operations jffs_address_operations;

kmem_cache_t     *node_cache = NULL;
kmem_cache_t     *fm_cache = NULL;

/* Called by the VFS at mount time to initialize the whole file system.  */
static int jffs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct jffs_control *c;

	sb->s_flags |= MS_NODIRATIME;

	D1(printk(KERN_NOTICE "JFFS: Trying to mount device %s.\n",
		  sb->s_id));

	if (MAJOR(sb->s_dev) != MTD_BLOCK_MAJOR) {
		printk(KERN_WARNING "JFFS: Trying to mount a "
		       "non-mtd device.\n");
		return -EINVAL;
	}

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_fs_info = (void *) 0;
	sb->s_maxbytes = 0xFFFFFFFF;

	/* Build the file system.  */
	if (jffs_build_fs(sb) < 0) {
		goto jffs_sb_err1;
	}

	/*
	 * set up enough so that we can read an inode
	 */
	sb->s_magic = JFFS_MAGIC_SB_BITMASK;
	sb->s_op = &jffs_ops;

	root_inode = iget(sb, JFFS_MIN_INO);
	if (!root_inode)
	        goto jffs_sb_err2;

	/* Get the root directory of this file system.  */
	if (!(sb->s_root = d_alloc_root(root_inode))) {
		goto jffs_sb_err3;
	}

	c = (struct jffs_control *) sb->s_fs_info;

#ifdef CONFIG_JFFS_PROC_FS
	/* Set up the jffs proc file system.  */
	if (jffs_register_jffs_proc_dir(MINOR(sb->s_dev), c) < 0) {
		printk(KERN_WARNING "JFFS: Failed to initialize the JFFS "
			"proc file system for device %s.\n",
			sb->s_id);
	}
#endif

	/* Set the Garbage Collection thresholds */

	/* GC if free space goes below 5% of the total size */
	c->gc_minfree_threshold = c->fmc->flash_size / 20;

	if (c->gc_minfree_threshold < c->fmc->sector_size)
		c->gc_minfree_threshold = c->fmc->sector_size;

	/* GC if dirty space exceeds 33% of the total size. */
	c->gc_maxdirty_threshold = c->fmc->flash_size / 3;

	if (c->gc_maxdirty_threshold < c->fmc->sector_size)
		c->gc_maxdirty_threshold = c->fmc->sector_size;


	c->thread_pid = kernel_thread (jffs_garbage_collect_thread, 
				        (void *) c, 
				        CLONE_KERNEL);
	D1(printk(KERN_NOTICE "JFFS: GC thread pid=%d.\n", (int) c->thread_pid));

	D1(printk(KERN_NOTICE "JFFS: Successfully mounted device %s.\n",
	       sb->s_id));
	return 0;

jffs_sb_err3:
	iput(root_inode);
jffs_sb_err2:
	jffs_cleanup_control((struct jffs_control *)sb->s_fs_info);
jffs_sb_err1:
	printk(KERN_WARNING "JFFS: Failed to mount device %s.\n",
	       sb->s_id);
	return -EINVAL;
}


/* This function is called when the file system is umounted.  */
static void
jffs_put_super(struct super_block *sb)
{
	struct jffs_control *c = (struct jffs_control *) sb->s_fs_info;

	D2(printk("jffs_put_super()\n"));

#ifdef CONFIG_JFFS_PROC_FS
	jffs_unregister_jffs_proc_dir(c);
#endif

	if (c->gc_task) {
		D1(printk (KERN_NOTICE "jffs_put_super(): Telling gc thread to die.\n"));
		send_sig(SIGKILL, c->gc_task, 1);
	}
	wait_for_completion(&c->gc_thread_comp);

	D1(printk (KERN_NOTICE "jffs_put_super(): Successfully waited on thread.\n"));

	jffs_cleanup_control((struct jffs_control *)sb->s_fs_info);
	D1(printk(KERN_NOTICE "JFFS: Successfully unmounted device %s.\n",
	       sb->s_id));
}


/* This function is called when user commands like chmod, chgrp and
   chown are executed. System calls like trunc() results in a call
   to this function.  */
static int
jffs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_fmcontrol *fmc;
	struct jffs_file *f;
	struct jffs_node *new_node;
	int update_all;
	int res = 0;
	int recoverable = 0;

	lock_kernel();

	if ((res = inode_change_ok(inode, iattr))) 
		goto out;

	c = (struct jffs_control *)inode->i_sb->s_fs_info;
	fmc = c->fmc;

	D3(printk (KERN_NOTICE "notify_change(): down biglock\n"));
	mutex_lock(&fmc->biglock);

	f = jffs_find_file(c, inode->i_ino);

	ASSERT(if (!f) {
		printk("jffs_setattr(): Invalid inode number: %lu\n",
		       inode->i_ino);
		D3(printk (KERN_NOTICE "notify_change(): up biglock\n"));
		mutex_unlock(&fmc->biglock);
		res = -EINVAL;
		goto out;
	});

	D1(printk("***jffs_setattr(): file: \"%s\", ino: %u\n",
		  f->name, f->ino));

	update_all = iattr->ia_valid & ATTR_FORCE;

	if ( (update_all || iattr->ia_valid & ATTR_SIZE)
	     && (iattr->ia_size + 128 < f->size) ) {
		/* We're shrinking the file by more than 128 bytes.
		   We'll be able to GC and recover this space, so
		   allow it to go into the reserved space. */
		recoverable = 1;
        }

	if (!(new_node = jffs_alloc_node())) {
		D(printk("jffs_setattr(): Allocation failed!\n"));
		D3(printk (KERN_NOTICE "notify_change(): up biglock\n"));
		mutex_unlock(&fmc->biglock);
		res = -ENOMEM;
		goto out;
	}

	new_node->data_offset = 0;
	new_node->removed_size = 0;
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = f->ino;
	raw_inode.pino = f->pino;
	raw_inode.mode = f->mode;
	raw_inode.uid = f->uid;
	raw_inode.gid = f->gid;
	raw_inode.atime = f->atime;
	raw_inode.mtime = f->mtime;
	raw_inode.ctime = f->ctime;
	raw_inode.dsize = 0;
	raw_inode.offset = 0;
	raw_inode.rsize = 0;
	raw_inode.dsize = 0;
	raw_inode.nsize = f->nsize;
	raw_inode.nlink = f->nlink;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	if (update_all || iattr->ia_valid & ATTR_MODE) {
		raw_inode.mode = iattr->ia_mode;
		inode->i_mode = iattr->ia_mode;
	}
	if (update_all || iattr->ia_valid & ATTR_UID) {
		raw_inode.uid = iattr->ia_uid;
		inode->i_uid = iattr->ia_uid;
	}
	if (update_all || iattr->ia_valid & ATTR_GID) {
		raw_inode.gid = iattr->ia_gid;
		inode->i_gid = iattr->ia_gid;
	}
	if (update_all || iattr->ia_valid & ATTR_SIZE) {
		int len;
		D1(printk("jffs_notify_change(): Changing size "
			  "to %lu bytes!\n", (long)iattr->ia_size));
		raw_inode.offset = iattr->ia_size;

		/* Calculate how many bytes need to be removed from
		   the end.  */
		if (f->size < iattr->ia_size) {
			len = 0;
		}
		else {
			len = f->size - iattr->ia_size;
		}

		raw_inode.rsize = len;

		/* The updated node will be a removal node, with
		   base at the new size and size of the nbr of bytes
		   to be removed.  */
		new_node->data_offset = iattr->ia_size;
		new_node->removed_size = len;
		inode->i_size = iattr->ia_size;
		inode->i_blocks = (inode->i_size + 511) >> 9;

		if (len) {
			invalidate_inode_pages(inode->i_mapping);
		}
		inode->i_ctime = CURRENT_TIME_SEC;
		inode->i_mtime = inode->i_ctime;
	}
	if (update_all || iattr->ia_valid & ATTR_ATIME) {
		raw_inode.atime = iattr->ia_atime.tv_sec;
		inode->i_atime = iattr->ia_atime;
	}
	if (update_all || iattr->ia_valid & ATTR_MTIME) {
		raw_inode.mtime = iattr->ia_mtime.tv_sec;
		inode->i_mtime = iattr->ia_mtime;
	}
	if (update_all || iattr->ia_valid & ATTR_CTIME) {
		raw_inode.ctime = iattr->ia_ctime.tv_sec;
		inode->i_ctime = iattr->ia_ctime;
	}

	/* Write this node to the flash.  */
	if ((res = jffs_write_node(c, new_node, &raw_inode, f->name, NULL, recoverable, f)) < 0) {
		D(printk("jffs_notify_change(): The write failed!\n"));
		jffs_free_node(new_node);
		D3(printk (KERN_NOTICE "n_c(): up biglock\n"));
		mutex_unlock(&c->fmc->biglock);
		goto out;
	}

	jffs_insert_node(c, f, &raw_inode, NULL, new_node);

	mark_inode_dirty(inode);
	D3(printk (KERN_NOTICE "n_c(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
out:
	unlock_kernel();
	return res;
} /* jffs_notify_change()  */


static struct inode *
jffs_new_inode(const struct inode * dir, struct jffs_raw_inode *raw_inode,
	       int * err)
{
	struct super_block * sb;
	struct inode * inode;
	struct jffs_control *c;
	struct jffs_file *f;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode) {
		*err = -ENOMEM;
		return NULL;
	}

	c = (struct jffs_control *)sb->s_fs_info;

	inode->i_ino = raw_inode->ino;
	inode->i_mode = raw_inode->mode;
	inode->i_nlink = raw_inode->nlink;
	inode->i_uid = raw_inode->uid;
	inode->i_gid = raw_inode->gid;
	inode->i_size = raw_inode->dsize;
	inode->i_atime.tv_sec = raw_inode->atime;
	inode->i_mtime.tv_sec = raw_inode->mtime;
	inode->i_ctime.tv_sec = raw_inode->ctime;
	inode->i_ctime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = (inode->i_size + 511) >> 9;

	f = jffs_find_file(c, raw_inode->ino);

	inode->u.generic_ip = (void *)f;
	insert_inode_hash(inode);

	return inode;
}

/* Get statistics of the file system.  */
static int
jffs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct jffs_control *c = (struct jffs_control *) sb->s_fs_info;
	struct jffs_fmcontrol *fmc;

	lock_kernel();

	fmc = c->fmc;

	D2(printk("jffs_statfs()\n"));

	buf->f_type = JFFS_MAGIC_SB_BITMASK;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_blocks = (fmc->flash_size / PAGE_CACHE_SIZE)
		       - (fmc->min_free_size / PAGE_CACHE_SIZE);
	buf->f_bfree = (jffs_free_size1(fmc) + jffs_free_size2(fmc) +
		       fmc->dirty_size - fmc->min_free_size)
			       >> PAGE_CACHE_SHIFT;
	buf->f_bavail = buf->f_bfree;

	/* Find out how many files there are in the filesystem.  */
	buf->f_files = jffs_foreach_file(c, jffs_file_count);
	buf->f_ffree = buf->f_bfree;
	/* buf->f_fsid = 0; */
	buf->f_namelen = JFFS_MAX_NAME_LEN;

	unlock_kernel();

	return 0;
}


/* Rename a file.  */
static int
jffs_rename(struct inode *old_dir, struct dentry *old_dentry,
	    struct inode *new_dir, struct dentry *new_dentry)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_file *old_dir_f;
	struct jffs_file *new_dir_f;
	struct jffs_file *del_f;
	struct jffs_file *f;
	struct jffs_node *node;
	struct inode *inode;
	int result = 0;
	__u32 rename_data = 0;

	D2(printk("***jffs_rename()\n"));

	D(printk("jffs_rename(): old_dir: 0x%p, old name: 0x%p, "
		 "new_dir: 0x%p, new name: 0x%p\n",
		 old_dir, old_dentry->d_name.name,
		 new_dir, new_dentry->d_name.name));

	lock_kernel();
	c = (struct jffs_control *)old_dir->i_sb->s_fs_info;
	ASSERT(if (!c) {
		printk(KERN_ERR "jffs_rename(): The old_dir inode "
		       "didn't have a reference to a jffs_file struct\n");
		unlock_kernel();
		return -EIO;
	});

	result = -ENOTDIR;
	if (!(old_dir_f = (struct jffs_file *)old_dir->u.generic_ip)) {
		D(printk("jffs_rename(): Old dir invalid.\n"));
		goto jffs_rename_end;
	}

	/* Try to find the file to move.  */
	result = -ENOENT;
	if (!(f = jffs_find_child(old_dir_f, old_dentry->d_name.name,
				  old_dentry->d_name.len))) {
		goto jffs_rename_end;
	}

	/* Find the new directory.  */
	result = -ENOTDIR;
	if (!(new_dir_f = (struct jffs_file *)new_dir->u.generic_ip)) {
		D(printk("jffs_rename(): New dir invalid.\n"));
		goto jffs_rename_end;
	}
	D3(printk (KERN_NOTICE "rename(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);
	/* Create a node and initialize as much as needed.  */
	result = -ENOMEM;
	if (!(node = jffs_alloc_node())) {
		D(printk("jffs_rename(): Allocation failed: node == 0\n"));
		goto jffs_rename_end;
	}
	node->data_offset = 0;
	node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = f->ino;
	raw_inode.pino = new_dir_f->ino;
/*  	raw_inode.version = f->highest_version + 1; */
	raw_inode.mode = f->mode;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = current->fsgid;
#if 0
	raw_inode.uid = f->uid;
	raw_inode.gid = f->gid;
#endif
	raw_inode.atime = get_seconds();
	raw_inode.mtime = raw_inode.atime;
	raw_inode.ctime = f->ctime;
	raw_inode.offset = 0;
	raw_inode.dsize = 0;
	raw_inode.rsize = 0;
	raw_inode.nsize = new_dentry->d_name.len;
	raw_inode.nlink = f->nlink;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	/* See if there already exists a file with the same name as
	   new_name.  */
	if ((del_f = jffs_find_child(new_dir_f, new_dentry->d_name.name,
				     new_dentry->d_name.len))) {
		raw_inode.rename = 1;
		raw_inode.dsize = sizeof(__u32);
		rename_data = del_f->ino;
	}

	/* Write the new node to the flash memory.  */
	if ((result = jffs_write_node(c, node, &raw_inode,
				      new_dentry->d_name.name,
				      (unsigned char*)&rename_data, 0, f)) < 0) {
		D(printk("jffs_rename(): Failed to write node to flash.\n"));
		jffs_free_node(node);
		goto jffs_rename_end;
	}
	raw_inode.dsize = 0;

	if (raw_inode.rename) {
		/* The file with the same name must be deleted.  */
		//FIXME deadlock	        down(&c->fmc->gclock);
		if ((result = jffs_remove(new_dir, new_dentry,
					  del_f->mode)) < 0) {
			/* This is really bad.  */
			printk(KERN_ERR "JFFS: An error occurred in "
			       "rename().\n");
		}
		//		up(&c->fmc->gclock);
	}

	if (old_dir_f != new_dir_f) {
		/* Remove the file from its old position in the
		   filesystem tree.  */
		jffs_unlink_file_from_tree(f);
	}

	/* Insert the new node into the file system.  */
	if ((result = jffs_insert_node(c, f, &raw_inode,
				       new_dentry->d_name.name, node)) < 0) {
		D(printk(KERN_ERR "jffs_rename(): jffs_insert_node() "
			 "failed!\n"));
	}

	if (old_dir_f != new_dir_f) {
		/* Insert the file to its new position in the
		   file system.  */
		jffs_insert_file_into_tree(f);
	}

	/* This is a kind of update of the inode we're about to make
	   here.  This is what they do in ext2fs.  Kind of.  */
	if ((inode = iget(new_dir->i_sb, f->ino))) {
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
		iput(inode);
	}

jffs_rename_end:
	D3(printk (KERN_NOTICE "rename(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return result;
} /* jffs_rename()  */


/* Read the contents of a directory.  Used by programs like `ls'
   for instance.  */
static int
jffs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct jffs_file *f;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct jffs_control *c = (struct jffs_control *)inode->i_sb->s_fs_info;
	int j;
	int ddino;
	lock_kernel();
	D3(printk (KERN_NOTICE "readdir(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	D2(printk("jffs_readdir(): inode: 0x%p, filp: 0x%p\n", inode, filp));
	if (filp->f_pos == 0) {
		D3(printk("jffs_readdir(): \".\" %lu\n", inode->i_ino));
		if (filldir(dirent, ".", 1, filp->f_pos, inode->i_ino, DT_DIR) < 0) {
			D3(printk (KERN_NOTICE "readdir(): up biglock\n"));
			mutex_unlock(&c->fmc->biglock);
			unlock_kernel();
			return 0;
		}
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (inode->i_ino == JFFS_MIN_INO) {
			ddino = JFFS_MIN_INO;
		}
		else {
			ddino = ((struct jffs_file *)
				 inode->u.generic_ip)->pino;
		}
		D3(printk("jffs_readdir(): \"..\" %u\n", ddino));
		if (filldir(dirent, "..", 2, filp->f_pos, ddino, DT_DIR) < 0) {
			D3(printk (KERN_NOTICE "readdir(): up biglock\n"));
			mutex_unlock(&c->fmc->biglock);
			unlock_kernel();
			return 0;
		}
		filp->f_pos++;
	}
	f = ((struct jffs_file *)inode->u.generic_ip)->children;

	j = 2;
	while(f && (f->deleted || j++ < filp->f_pos )) {
		f = f->sibling_next;
	}

	while (f) {
		D3(printk("jffs_readdir(): \"%s\" ino: %u\n",
			  (f->name ? f->name : ""), f->ino));
		if (filldir(dirent, f->name, f->nsize,
			    filp->f_pos , f->ino, DT_UNKNOWN) < 0) {
		        D3(printk (KERN_NOTICE "readdir(): up biglock\n"));
			mutex_unlock(&c->fmc->biglock);
			unlock_kernel();
			return 0;
		}
		filp->f_pos++;
		do {
			f = f->sibling_next;
		} while(f && f->deleted);
	}
	D3(printk (KERN_NOTICE "readdir(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return filp->f_pos;
} /* jffs_readdir()  */


/* Find a file in a directory. If the file exists, return its
   corresponding dentry.  */
static struct dentry *
jffs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct jffs_file *d;
	struct jffs_file *f;
	struct jffs_control *c = (struct jffs_control *)dir->i_sb->s_fs_info;
	int len;
	int r = 0;
	const char *name;
	struct inode *inode = NULL;

	len = dentry->d_name.len;
	name = dentry->d_name.name;

	lock_kernel();

	D3({
		char *s = (char *)kmalloc(len + 1, GFP_KERNEL);
		memcpy(s, name, len);
		s[len] = '\0';
		printk("jffs_lookup(): dir: 0x%p, name: \"%s\"\n", dir, s);
		kfree(s);
	});

	D3(printk (KERN_NOTICE "lookup(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	r = -ENAMETOOLONG;
	if (len > JFFS_MAX_NAME_LEN) {
		goto jffs_lookup_end;
	}

	r = -EACCES;
	if (!(d = (struct jffs_file *)dir->u.generic_ip)) {
		D(printk("jffs_lookup(): No such inode! (%lu)\n",
			 dir->i_ino));
		goto jffs_lookup_end;
	}

	/* Get the corresponding inode to the file.  */

	/* iget calls jffs_read_inode, so we need to drop the biglock
           before calling iget.  Unfortunately, the GC has a tendency
           to sneak in here, because iget sometimes calls schedule ().
	*/

	if ((len == 1) && (name[0] == '.')) {
		D3(printk (KERN_NOTICE "lookup(): up biglock\n"));
		mutex_unlock(&c->fmc->biglock);
		if (!(inode = iget(dir->i_sb, d->ino))) {
			D(printk("jffs_lookup(): . iget() ==> NULL\n"));
			goto jffs_lookup_end_no_biglock;
		}
		D3(printk (KERN_NOTICE "lookup(): down biglock\n"));
		mutex_lock(&c->fmc->biglock);
	} else if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
	        D3(printk (KERN_NOTICE "lookup(): up biglock\n"));
		mutex_unlock(&c->fmc->biglock);
 		if (!(inode = iget(dir->i_sb, d->pino))) {
			D(printk("jffs_lookup(): .. iget() ==> NULL\n"));
			goto jffs_lookup_end_no_biglock;
		}
		D3(printk (KERN_NOTICE "lookup(): down biglock\n"));
		mutex_lock(&c->fmc->biglock);
	} else if ((f = jffs_find_child(d, name, len))) {
	        D3(printk (KERN_NOTICE "lookup(): up biglock\n"));
		mutex_unlock(&c->fmc->biglock);
		if (!(inode = iget(dir->i_sb, f->ino))) {
			D(printk("jffs_lookup(): iget() ==> NULL\n"));
			goto jffs_lookup_end_no_biglock;
		}
		D3(printk (KERN_NOTICE "lookup(): down biglock\n"));
		mutex_lock(&c->fmc->biglock);
	} else {
		D3(printk("jffs_lookup(): Couldn't find the file. "
			  "f = 0x%p, name = \"%s\", d = 0x%p, d->ino = %u\n",
			  f, name, d, d->ino));
		inode = NULL;
	}

	d_add(dentry, inode);
	D3(printk (KERN_NOTICE "lookup(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return NULL;

jffs_lookup_end:
	D3(printk (KERN_NOTICE "lookup(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);

jffs_lookup_end_no_biglock:
	unlock_kernel();
	return ERR_PTR(r);
} /* jffs_lookup()  */


/* Try to read a page of data from a file.  */
static int
jffs_do_readpage_nolock(struct file *file, struct page *page)
{
	void *buf;
	unsigned long read_len;
	int result;
	struct inode *inode = (struct inode*)page->mapping->host;
	struct jffs_file *f = (struct jffs_file *)inode->u.generic_ip;
	struct jffs_control *c = (struct jffs_control *)inode->i_sb->s_fs_info;
	int r;
	loff_t offset;

	D2(printk("***jffs_readpage(): file = \"%s\", page->index = %lu\n",
		  (f->name ? f->name : ""), (long)page->index));

	get_page(page);
	/* Don't SetPageLocked(page), should be locked already */
	ClearPageUptodate(page);
	ClearPageError(page);

	D3(printk (KERN_NOTICE "readpage(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	read_len = 0;
	result = 0;
	offset = page_offset(page);

	kmap(page);
	buf = page_address(page);
	if (offset < inode->i_size) {
		read_len = min_t(long, inode->i_size - offset, PAGE_SIZE);
		r = jffs_read_data(f, buf, offset, read_len);
		if (r != read_len) {
			result = -EIO;
			D(
			        printk("***jffs_readpage(): Read error! "
				       "Wanted to read %lu bytes but only "
				       "read %d bytes.\n", read_len, r);
			  );
		}

	}

	/* This handles the case of partial or no read in above */
	if(read_len < PAGE_SIZE)
	        memset(buf + read_len, 0, PAGE_SIZE - read_len);
	flush_dcache_page(page);
	kunmap(page);

	D3(printk (KERN_NOTICE "readpage(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);

	if (result) {
	        SetPageError(page);
	}else {
	        SetPageUptodate(page);	        
	}

	page_cache_release(page);

	D3(printk("jffs_readpage(): Leaving...\n"));

	return result;
} /* jffs_do_readpage_nolock()  */

static int jffs_readpage(struct file *file, struct page *page)
{
	int ret = jffs_do_readpage_nolock(file, page);
	unlock_page(page);
	return ret;
}

/* Create a new directory.  */
static int
jffs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_node *node;
	struct jffs_file *dir_f;
	struct inode *inode;
	int dir_mode;
	int result = 0;
	int err;

	D1({
	        int len = dentry->d_name.len;
		char *_name = (char *) kmalloc(len + 1, GFP_KERNEL);
		memcpy(_name, dentry->d_name.name, len);
		_name[len] = '\0';
		printk("***jffs_mkdir(): dir = 0x%p, name = \"%s\", "
		       "len = %d, mode = 0x%08x\n", dir, _name, len, mode);
		kfree(_name);
	});

	lock_kernel();
	dir_f = (struct jffs_file *)dir->u.generic_ip;

	ASSERT(if (!dir_f) {
		printk(KERN_ERR "jffs_mkdir(): No reference to a "
		       "jffs_file struct in inode.\n");
		unlock_kernel();
		return -EIO;
	});

	c = dir_f->c;
	D3(printk (KERN_NOTICE "mkdir(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	dir_mode = S_IFDIR | (mode & (S_IRWXUGO|S_ISVTX)
			      & ~current->fs->umask);
	if (dir->i_mode & S_ISGID) {
		dir_mode |= S_ISGID;
	}

	/* Create a node and initialize it as much as needed.  */
	if (!(node = jffs_alloc_node())) {
		D(printk("jffs_mkdir(): Allocation failed: node == 0\n"));
		result = -ENOMEM;
		goto jffs_mkdir_end;
	}
	node->data_offset = 0;
	node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = c->next_ino++;
	raw_inode.pino = dir_f->ino;
	raw_inode.version = 1;
	raw_inode.mode = dir_mode;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	/*	raw_inode.gid = current->fsgid; */
	raw_inode.atime = get_seconds();
	raw_inode.mtime = raw_inode.atime;
	raw_inode.ctime = raw_inode.atime;
	raw_inode.offset = 0;
	raw_inode.dsize = 0;
	raw_inode.rsize = 0;
	raw_inode.nsize = dentry->d_name.len;
	raw_inode.nlink = 1;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	/* Write the new node to the flash.  */
	if ((result = jffs_write_node(c, node, &raw_inode,
				     dentry->d_name.name, NULL, 0, NULL)) < 0) {
		D(printk("jffs_mkdir(): jffs_write_node() failed.\n"));
		jffs_free_node(node);
		goto jffs_mkdir_end;
	}

	/* Insert the new node into the file system.  */
	if ((result = jffs_insert_node(c, NULL, &raw_inode, dentry->d_name.name,
				       node)) < 0) {
		goto jffs_mkdir_end;
	}

	inode = jffs_new_inode(dir, &raw_inode, &err);
	if (inode == NULL) {
		result = err;
		goto jffs_mkdir_end;
	}

	inode->i_op = &jffs_dir_inode_operations;
	inode->i_fop = &jffs_dir_operations;

	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);

	result = 0;
jffs_mkdir_end:
	D3(printk (KERN_NOTICE "mkdir(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return result;
} /* jffs_mkdir()  */


/* Remove a directory.  */
static int
jffs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct jffs_control *c = (struct jffs_control *)dir->i_sb->s_fs_info;
	int ret;
	D3(printk("***jffs_rmdir()\n"));
	D3(printk (KERN_NOTICE "rmdir(): down biglock\n"));
	lock_kernel();
	mutex_lock(&c->fmc->biglock);
	ret = jffs_remove(dir, dentry, S_IFDIR);
	D3(printk (KERN_NOTICE "rmdir(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return ret;
}


/* Remove any kind of file except for directories.  */
static int
jffs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct jffs_control *c = (struct jffs_control *)dir->i_sb->s_fs_info;
	int ret; 

	lock_kernel();
	D3(printk("***jffs_unlink()\n"));
	D3(printk (KERN_NOTICE "unlink(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);
	ret = jffs_remove(dir, dentry, 0);
	D3(printk (KERN_NOTICE "unlink(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return ret;
}


/* Remove a JFFS entry, i.e. plain files, directories, etc.  Here we
   shouldn't test for free space on the device.  */
static int
jffs_remove(struct inode *dir, struct dentry *dentry, int type)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_file *dir_f; /* The file-to-remove's parent.  */
	struct jffs_file *del_f; /* The file to remove.  */
	struct jffs_node *del_node;
	struct inode *inode = NULL;
	int result = 0;

	D1({
		int len = dentry->d_name.len;
		const char *name = dentry->d_name.name;
		char *_name = (char *) kmalloc(len + 1, GFP_KERNEL);
		memcpy(_name, name, len);
		_name[len] = '\0';
		printk("***jffs_remove(): file = \"%s\", ino = %ld\n", _name, dentry->d_inode->i_ino);
		kfree(_name);
	});

	dir_f = (struct jffs_file *) dir->u.generic_ip;
	c = dir_f->c;

	result = -ENOENT;
	if (!(del_f = jffs_find_child(dir_f, dentry->d_name.name,
				      dentry->d_name.len))) {
		D(printk("jffs_remove(): jffs_find_child() failed.\n"));
		goto jffs_remove_end;
	}

	if (S_ISDIR(type)) {
		struct jffs_file *child = del_f->children;
		while(child) {
			if( !child->deleted ) {
				result = -ENOTEMPTY;
				goto jffs_remove_end;
			}
			child = child->sibling_next;
		}
	}            
	else if (S_ISDIR(del_f->mode)) {
		D(printk("jffs_remove(): node is a directory "
			 "but it shouldn't be.\n"));
		result = -EPERM;
		goto jffs_remove_end;
	}

	inode = dentry->d_inode;

	result = -EIO;
	if (del_f->ino != inode->i_ino)
		goto jffs_remove_end;

	if (!inode->i_nlink) {
		printk("Deleting nonexistent file inode: %lu, nlink: %d\n",
		       inode->i_ino, inode->i_nlink);
		inode->i_nlink=1;
	}

	/* Create a node for the deletion.  */
	result = -ENOMEM;
	if (!(del_node = jffs_alloc_node())) {
		D(printk("jffs_remove(): Allocation failed!\n"));
		goto jffs_remove_end;
	}
	del_node->data_offset = 0;
	del_node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = del_f->ino;
	raw_inode.pino = del_f->pino;
/*  	raw_inode.version = del_f->highest_version + 1; */
	raw_inode.mode = del_f->mode;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = current->fsgid;
	raw_inode.atime = get_seconds();
	raw_inode.mtime = del_f->mtime;
	raw_inode.ctime = raw_inode.atime;
	raw_inode.offset = 0;
	raw_inode.dsize = 0;
	raw_inode.rsize = 0;
	raw_inode.nsize = 0;
	raw_inode.nlink = del_f->nlink;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 1;

	/* Write the new node to the flash memory.  */
	if (jffs_write_node(c, del_node, &raw_inode, NULL, NULL, 1, del_f) < 0) {
		jffs_free_node(del_node);
		result = -EIO;
		goto jffs_remove_end;
	}

	/* Update the file.  This operation will make the file disappear
	   from the in-memory file system structures.  */
	jffs_insert_node(c, del_f, &raw_inode, NULL, del_node);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);

	d_delete(dentry);	/* This also frees the inode */

	result = 0;
jffs_remove_end:
	return result;
} /* jffs_remove()  */


static int
jffs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_file *dir_f;
	struct jffs_node *node = NULL;
	struct jffs_control *c;
	struct inode *inode;
	int result = 0;
	u16 data = old_encode_dev(rdev);
	int err;

	D1(printk("***jffs_mknod()\n"));

	if (!old_valid_dev(rdev))
		return -EINVAL;
	lock_kernel();
	dir_f = (struct jffs_file *)dir->u.generic_ip;
	c = dir_f->c;

	D3(printk (KERN_NOTICE "mknod(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	/* Create and initialize a new node.  */
	if (!(node = jffs_alloc_node())) {
		D(printk("jffs_mknod(): Allocation failed!\n"));
		result = -ENOMEM;
		goto jffs_mknod_err;
	}
	node->data_offset = 0;
	node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = c->next_ino++;
	raw_inode.pino = dir_f->ino;
	raw_inode.version = 1;
	raw_inode.mode = mode;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	/*	raw_inode.gid = current->fsgid; */
	raw_inode.atime = get_seconds();
	raw_inode.mtime = raw_inode.atime;
	raw_inode.ctime = raw_inode.atime;
	raw_inode.offset = 0;
	raw_inode.dsize = 2;
	raw_inode.rsize = 0;
	raw_inode.nsize = dentry->d_name.len;
	raw_inode.nlink = 1;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	/* Write the new node to the flash.  */
	if ((err = jffs_write_node(c, node, &raw_inode, dentry->d_name.name,
				   (unsigned char *)&data, 0, NULL)) < 0) {
		D(printk("jffs_mknod(): jffs_write_node() failed.\n"));
		result = err;
		goto jffs_mknod_err;
	}

	/* Insert the new node into the file system.  */
	if ((err = jffs_insert_node(c, NULL, &raw_inode, dentry->d_name.name,
				    node)) < 0) {
		result = err;
		goto jffs_mknod_end;
	}

	inode = jffs_new_inode(dir, &raw_inode, &err);
	if (inode == NULL) {
		result = err;
		goto jffs_mknod_end;
	}

	init_special_inode(inode, mode, rdev);

	d_instantiate(dentry, inode);

	goto jffs_mknod_end;

jffs_mknod_err:
	if (node) {
		jffs_free_node(node);
	}

jffs_mknod_end:
	D3(printk (KERN_NOTICE "mknod(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return result;
} /* jffs_mknod()  */


static int
jffs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_file *dir_f;
	struct jffs_node *node;
	struct inode *inode;

	int symname_len = strlen(symname);
	int err;

	lock_kernel();
	D1({
		int len = dentry->d_name.len; 
		char *_name = (char *)kmalloc(len + 1, GFP_KERNEL);
		char *_symname = (char *)kmalloc(symname_len + 1, GFP_KERNEL);
		memcpy(_name, dentry->d_name.name, len);
		_name[len] = '\0';
		memcpy(_symname, symname, symname_len);
		_symname[symname_len] = '\0';
		printk("***jffs_symlink(): dir = 0x%p, "
		       "dentry->dname.name = \"%s\", "
		       "symname = \"%s\"\n", dir, _name, _symname);
		kfree(_name);
		kfree(_symname);
	});

	dir_f = (struct jffs_file *)dir->u.generic_ip;
	ASSERT(if (!dir_f) {
		printk(KERN_ERR "jffs_symlink(): No reference to a "
		       "jffs_file struct in inode.\n");
		unlock_kernel();
		return -EIO;
	});

	c = dir_f->c;

	/* Create a node and initialize it as much as needed.  */
	if (!(node = jffs_alloc_node())) {
		D(printk("jffs_symlink(): Allocation failed: node = NULL\n"));
		unlock_kernel();
		return -ENOMEM;
	}
	D3(printk (KERN_NOTICE "symlink(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	node->data_offset = 0;
	node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = c->next_ino++;
	raw_inode.pino = dir_f->ino;
	raw_inode.version = 1;
	raw_inode.mode = S_IFLNK | S_IRWXUGO;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	raw_inode.atime = get_seconds();
	raw_inode.mtime = raw_inode.atime;
	raw_inode.ctime = raw_inode.atime;
	raw_inode.offset = 0;
	raw_inode.dsize = symname_len;
	raw_inode.rsize = 0;
	raw_inode.nsize = dentry->d_name.len;
	raw_inode.nlink = 1;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	/* Write the new node to the flash.  */
	if ((err = jffs_write_node(c, node, &raw_inode, dentry->d_name.name,
				   (const unsigned char *)symname, 0, NULL)) < 0) {
		D(printk("jffs_symlink(): jffs_write_node() failed.\n"));
		jffs_free_node(node);
		goto jffs_symlink_end;
	}

	/* Insert the new node into the file system.  */
	if ((err = jffs_insert_node(c, NULL, &raw_inode, dentry->d_name.name,
				    node)) < 0) {
		goto jffs_symlink_end;
	}

	inode = jffs_new_inode(dir, &raw_inode, &err);
	if (inode == NULL) {
		goto jffs_symlink_end;
	}
	err = 0;
	inode->i_op = &page_symlink_inode_operations;
	inode->i_mapping->a_ops = &jffs_address_operations;

	d_instantiate(dentry, inode);
 jffs_symlink_end:
	D3(printk (KERN_NOTICE "symlink(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return err;
} /* jffs_symlink()  */


/* Create an inode inside a JFFS directory (dir) and return it.
 *
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int
jffs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_node *node;
	struct jffs_file *dir_f; /* JFFS representation of the directory.  */
	struct inode *inode;
	int err;

	lock_kernel();
	D1({
		int len = dentry->d_name.len;
		char *s = (char *)kmalloc(len + 1, GFP_KERNEL);
		memcpy(s, dentry->d_name.name, len);
		s[len] = '\0';
		printk("jffs_create(): dir: 0x%p, name: \"%s\"\n", dir, s);
		kfree(s);
	});

	dir_f = (struct jffs_file *)dir->u.generic_ip;
	ASSERT(if (!dir_f) {
		printk(KERN_ERR "jffs_create(): No reference to a "
		       "jffs_file struct in inode.\n");
		unlock_kernel();
		return -EIO;
	});

	c = dir_f->c;

	/* Create a node and initialize as much as needed.  */
	if (!(node = jffs_alloc_node())) {
		D(printk("jffs_create(): Allocation failed: node == 0\n"));
		unlock_kernel();
		return -ENOMEM;
	}
	D3(printk (KERN_NOTICE "create(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	node->data_offset = 0;
	node->removed_size = 0;

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = c->next_ino++;
	raw_inode.pino = dir_f->ino;
	raw_inode.version = 1;
	raw_inode.mode = mode;
	raw_inode.uid = current->fsuid;
	raw_inode.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	raw_inode.atime = get_seconds();
	raw_inode.mtime = raw_inode.atime;
	raw_inode.ctime = raw_inode.atime;
	raw_inode.offset = 0;
	raw_inode.dsize = 0;
	raw_inode.rsize = 0;
	raw_inode.nsize = dentry->d_name.len;
	raw_inode.nlink = 1;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;

	/* Write the new node to the flash.  */
	if ((err = jffs_write_node(c, node, &raw_inode,
				   dentry->d_name.name, NULL, 0, NULL)) < 0) {
		D(printk("jffs_create(): jffs_write_node() failed.\n"));
		jffs_free_node(node);
		goto jffs_create_end;
	}

	/* Insert the new node into the file system.  */
	if ((err = jffs_insert_node(c, NULL, &raw_inode, dentry->d_name.name,
				    node)) < 0) {
		goto jffs_create_end;
	}

	/* Initialize an inode.  */
	inode = jffs_new_inode(dir, &raw_inode, &err);
	if (inode == NULL) {
		goto jffs_create_end;
	}
	err = 0;
	inode->i_op = &jffs_file_inode_operations;
	inode->i_fop = &jffs_file_operations;
	inode->i_mapping->a_ops = &jffs_address_operations;
	inode->i_mapping->nrpages = 0;

	d_instantiate(dentry, inode);
 jffs_create_end:
	D3(printk (KERN_NOTICE "create(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	unlock_kernel();
	return err;
} /* jffs_create()  */


/* Write, append or rewrite data to an existing file.  */
static ssize_t
jffs_file_write(struct file *filp, const char *buf, size_t count,
		loff_t *ppos)
{
	struct jffs_raw_inode raw_inode;
	struct jffs_control *c;
	struct jffs_file *f;
	struct jffs_node *node;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	int recoverable = 0;
	size_t written = 0;
	__u32 thiscount = count;
	loff_t pos = *ppos;
	int err;

	inode = filp->f_dentry->d_inode;

	D2(printk("***jffs_file_write(): inode: 0x%p (ino: %lu), "
		  "filp: 0x%p, buf: 0x%p, count: %d\n",
		  inode, inode->i_ino, filp, buf, count));

#if 0
	if (inode->i_sb->s_flags & MS_RDONLY) {
		D(printk("jffs_file_write(): MS_RDONLY\n"));
		err = -EROFS;
		goto out_isem;
	}
#endif	
	err = -EINVAL;

	if (!S_ISREG(inode->i_mode)) {
		D(printk("jffs_file_write(): inode->i_mode == 0x%08x\n",
				inode->i_mode));
		goto out_isem;
	}

	if (!(f = (struct jffs_file *)inode->u.generic_ip)) {
		D(printk("jffs_file_write(): inode->u.generic_ip = 0x%p\n",
				inode->u.generic_ip));
		goto out_isem;
	}

	c = f->c;

	/*
	 * This will never trigger with sane page sizes.  leave it in
	 * anyway, since I'm thinking about how to merge larger writes
	 * (the current idea is to poke a thread that does the actual
	 * I/O and starts by doing a mutex_lock(&inode->i_mutex).  then we
	 * would need to get the page cache pages and have a list of
	 * I/O requests and do write-merging here.
	 * -- prumpf
	 */
	thiscount = min(c->fmc->max_chunk_size - sizeof(struct jffs_raw_inode), count);

	D3(printk (KERN_NOTICE "file_write(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	/* Urgh. POSIX says we can do short writes if we feel like it. 
	 * In practice, we can't. Nothing will cope. So we loop until
	 * we're done.
	 *
	 * <_Anarchy_> posix and reality are not interconnected on this issue
	 */
	while (count) {
		/* Things are going to be written so we could allocate and
		   initialize the necessary data structures now.  */
		if (!(node = jffs_alloc_node())) {
			D(printk("jffs_file_write(): node == 0\n"));
			err = -ENOMEM;
			goto out;
		}

		node->data_offset = pos;
		node->removed_size = 0;

		/* Initialize the raw inode.  */
		raw_inode.magic = JFFS_MAGIC_BITMASK;
		raw_inode.ino = f->ino;
		raw_inode.pino = f->pino;

		raw_inode.mode = f->mode;

		raw_inode.uid = f->uid;
		raw_inode.gid = f->gid;
		raw_inode.atime = get_seconds();
		raw_inode.mtime = raw_inode.atime;
		raw_inode.ctime = f->ctime;
		raw_inode.offset = pos;
		raw_inode.dsize = thiscount;
		raw_inode.rsize = 0;
		raw_inode.nsize = f->nsize;
		raw_inode.nlink = f->nlink;
		raw_inode.spare = 0;
		raw_inode.rename = 0;
		raw_inode.deleted = 0;

		if (pos < f->size) {
			node->removed_size = raw_inode.rsize = min(thiscount, (__u32)(f->size - pos));

			/* If this node is going entirely over the top of old data,
			   we can allow it to go into the reserved space, because
			   we know that GC can reclaim the space later.
			*/
			if (pos + thiscount < f->size) {
				/* If all the data we're overwriting are _real_,
				   not just holes, then:
				   recoverable = 1;
				*/
			}
		}

		/* Write the new node to the flash.  */
		/* NOTE: We would be quite happy if jffs_write_node() wrote a
		   smaller node than we were expecting. There's no need for it
		   to waste the space at the end of the flash just because it's
		   a little smaller than what we asked for. But that's a whole
		   new can of worms which I'm not going to open this week. 
		   -- dwmw2.
		*/
		if ((err = jffs_write_node(c, node, &raw_inode, f->name,
					   (const unsigned char *)buf,
					   recoverable, f)) < 0) {
			D(printk("jffs_file_write(): jffs_write_node() failed.\n"));
			jffs_free_node(node);
			goto out;
		}

		written += err;
		buf += err;
		count -= err;
		pos += err;

		/* Insert the new node into the file system.  */
		if ((err = jffs_insert_node(c, f, &raw_inode, NULL, node)) < 0) {
			goto out;
		}

		D3(printk("jffs_file_write(): new f_pos %ld.\n", (long)pos));

		thiscount = min(c->fmc->max_chunk_size - sizeof(struct jffs_raw_inode), count);
	}
 out:
	D3(printk (KERN_NOTICE "file_write(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);

	/* Fix things in the real inode.  */
	if (pos > inode->i_size) {
		inode->i_size = pos;
		inode->i_blocks = (inode->i_size + 511) >> 9;
	}
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	invalidate_inode_pages(inode->i_mapping);

 out_isem:
	return err;
} /* jffs_file_write()  */

static int
jffs_prepare_write(struct file *filp, struct page *page,
                  unsigned from, unsigned to)
{
	/* FIXME: we should detect some error conditions here */

	/* Bugger that. We should make sure the page is uptodate */
	if (!PageUptodate(page) && (from || to < PAGE_CACHE_SIZE))
		return jffs_do_readpage_nolock(filp, page);

	return 0;
} /* jffs_prepare_write() */

static int
jffs_commit_write(struct file *filp, struct page *page,
                 unsigned from, unsigned to)
{
       void *addr = page_address(page) + from;
       /* XXX: PAGE_CACHE_SHIFT or PAGE_SHIFT */
       loff_t pos = page_offset(page) + from;

       return jffs_file_write(filp, addr, to-from, &pos);
} /* jffs_commit_write() */

/* This is our ioctl() routine.  */
static int
jffs_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	   unsigned long arg)
{
	struct jffs_control *c;
	int ret = 0;

	D2(printk("***jffs_ioctl(): cmd = 0x%08x, arg = 0x%08lx\n",
		  cmd, arg));

	if (!(c = (struct jffs_control *)inode->i_sb->s_fs_info)) {
		printk(KERN_ERR "JFFS: Bad inode in ioctl() call. "
		       "(cmd = 0x%08x)\n", cmd);
		return -EIO;
	}
	D3(printk (KERN_NOTICE "ioctl(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);

	switch (cmd) {
	case JFFS_PRINT_HASH:
		jffs_print_hash_table(c);
		break;
	case JFFS_PRINT_TREE:
		jffs_print_tree(c->root, 0);
		break;
	case JFFS_GET_STATUS:
		{
			struct jffs_flash_status fst;
			struct jffs_fmcontrol *fmc = c->fmc;
			printk("Flash status -- ");
			if (!access_ok(VERIFY_WRITE,
				       (struct jffs_flash_status __user *)arg,
				       sizeof(struct jffs_flash_status))) {
				D(printk("jffs_ioctl(): Bad arg in "
					 "JFFS_GET_STATUS ioctl!\n"));
				ret = -EFAULT;
				break;
			}
			fst.size = fmc->flash_size;
			fst.used = fmc->used_size;
			fst.dirty = fmc->dirty_size;
			fst.begin = fmc->head->offset;
			fst.end = fmc->tail->offset + fmc->tail->size;
			printk("size: %d, used: %d, dirty: %d, "
			       "begin: %d, end: %d\n",
			       fst.size, fst.used, fst.dirty,
			       fst.begin, fst.end);
			if (copy_to_user((struct jffs_flash_status __user *)arg,
					 &fst,
					 sizeof(struct jffs_flash_status))) {
				ret = -EFAULT;
			}
		}
		break;
	default:
		ret = -ENOTTY;
	}
	D3(printk (KERN_NOTICE "ioctl(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
	return ret;
} /* jffs_ioctl()  */


static struct address_space_operations jffs_address_operations = {
	.readpage	= jffs_readpage,
	.prepare_write	= jffs_prepare_write,
	.commit_write	= jffs_commit_write,
};

static int jffs_fsync(struct file *f, struct dentry *d, int datasync)
{
	/* We currently have O_SYNC operations at all times.
	   Do nothing.
	*/
	return 0;
}


static const struct file_operations jffs_file_operations =
{
	.open		= generic_file_open,
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= generic_file_write,
	.ioctl		= jffs_ioctl,
	.mmap		= generic_file_readonly_mmap,
	.fsync		= jffs_fsync,
	.sendfile	= generic_file_sendfile,
};


static struct inode_operations jffs_file_inode_operations =
{
	.lookup		= jffs_lookup,          /* lookup */
	.setattr	= jffs_setattr,
};


static const struct file_operations jffs_dir_operations =
{
	.readdir	= jffs_readdir,
};


static struct inode_operations jffs_dir_inode_operations =
{
	.create		= jffs_create,
	.lookup		= jffs_lookup,
	.unlink		= jffs_unlink,
	.symlink	= jffs_symlink,
	.mkdir		= jffs_mkdir,
	.rmdir		= jffs_rmdir,
	.mknod		= jffs_mknod,
	.rename		= jffs_rename,
	.setattr	= jffs_setattr,
};


/* Initialize an inode for the VFS.  */
static void
jffs_read_inode(struct inode *inode)
{
	struct jffs_file *f;
	struct jffs_control *c;

	D3(printk("jffs_read_inode(): inode->i_ino == %lu\n", inode->i_ino));

	if (!inode->i_sb) {
		D(printk("jffs_read_inode(): !inode->i_sb ==> "
			 "No super block!\n"));
		return;
	}
	c = (struct jffs_control *)inode->i_sb->s_fs_info;
	D3(printk (KERN_NOTICE "read_inode(): down biglock\n"));
	mutex_lock(&c->fmc->biglock);
	if (!(f = jffs_find_file(c, inode->i_ino))) {
		D(printk("jffs_read_inode(): No such inode (%lu).\n",
			 inode->i_ino));
		D3(printk (KERN_NOTICE "read_inode(): up biglock\n"));
		mutex_unlock(&c->fmc->biglock);
		return;
	}
	inode->u.generic_ip = (void *)f;
	inode->i_mode = f->mode;
	inode->i_nlink = f->nlink;
	inode->i_uid = f->uid;
	inode->i_gid = f->gid;
	inode->i_size = f->size;
	inode->i_atime.tv_sec = f->atime;
	inode->i_mtime.tv_sec = f->mtime;
	inode->i_ctime.tv_sec = f->ctime;
	inode->i_atime.tv_nsec = 
	inode->i_mtime.tv_nsec = 
	inode->i_ctime.tv_nsec = 0;

	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = (inode->i_size + 511) >> 9;
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &jffs_file_inode_operations;
		inode->i_fop = &jffs_file_operations;
		inode->i_mapping->a_ops = &jffs_address_operations;
	}
	else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &jffs_dir_inode_operations;
		inode->i_fop = &jffs_dir_operations;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &page_symlink_inode_operations;
		inode->i_mapping->a_ops = &jffs_address_operations;
	}
	else {
		/* If the node is a device of some sort, then the number of
		   the device should be read from the flash memory and then
		   added to the inode's i_rdev member.  */
		u16 val;
		jffs_read_data(f, (char *)&val, 0, 2);
		init_special_inode(inode, inode->i_mode,
			old_decode_dev(val));
	}

	D3(printk (KERN_NOTICE "read_inode(): up biglock\n"));
	mutex_unlock(&c->fmc->biglock);
}


static void
jffs_delete_inode(struct inode *inode)
{
	struct jffs_file *f;
	struct jffs_control *c;
	D3(printk("jffs_delete_inode(): inode->i_ino == %lu\n",
		  inode->i_ino));

	truncate_inode_pages(&inode->i_data, 0);
	lock_kernel();
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->u.generic_ip = NULL;
	clear_inode(inode);
	if (inode->i_nlink == 0) {
		c = (struct jffs_control *) inode->i_sb->s_fs_info;
		f = (struct jffs_file *) jffs_find_file (c, inode->i_ino);
		jffs_possibly_delete_file(f);
	}

	unlock_kernel();
}


static void
jffs_write_super(struct super_block *sb)
{
	struct jffs_control *c = (struct jffs_control *)sb->s_fs_info;
	lock_kernel();
	jffs_garbage_collect_trigger(c);
	unlock_kernel();
}

static int jffs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	return 0;
}

static struct super_operations jffs_ops =
{
	.read_inode	= jffs_read_inode,
	.delete_inode 	= jffs_delete_inode,
	.put_super	= jffs_put_super,
	.write_super	= jffs_write_super,
	.statfs		= jffs_statfs,
	.remount_fs	= jffs_remount,
};

static int jffs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, jffs_fill_super,
			   mnt);
}

static struct file_system_type jffs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "jffs",
	.get_sb		= jffs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init
init_jffs_fs(void)
{
	printk(KERN_INFO "JFFS version " JFFS_VERSION_STRING
		", (C) 1999, 2000  Axis Communications AB\n");
	
#ifdef CONFIG_JFFS_PROC_FS
	jffs_proc_root = proc_mkdir("jffs", proc_root_fs);
	if (!jffs_proc_root) {
		printk(KERN_WARNING "cannot create /proc/jffs entry\n");
	}
#endif
	fm_cache = kmem_cache_create("jffs_fm", sizeof(struct jffs_fm),
		       0,
		       SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
		       NULL, NULL);
	if (!fm_cache) {
		return -ENOMEM;
	}

	node_cache = kmem_cache_create("jffs_node",sizeof(struct jffs_node),
		       0,
		       SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
		       NULL, NULL);
	if (!node_cache) {
		kmem_cache_destroy(fm_cache);
		return -ENOMEM;
	}

	return register_filesystem(&jffs_fs_type);
}

static void __exit
exit_jffs_fs(void)
{
	unregister_filesystem(&jffs_fs_type);
	kmem_cache_destroy(fm_cache);
	kmem_cache_destroy(node_cache);
}

module_init(init_jffs_fs)
module_exit(exit_jffs_fs)

MODULE_DESCRIPTION("The Journalling Flash File System");
MODULE_AUTHOR("Axis Communications AB.");
MODULE_LICENSE("GPL");
