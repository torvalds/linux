/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 * Acknowledgements:
 * Luc van OostenRyck for numerous patches.
 * Nick Bane for numerous patches.
 * Nick Bane for 2.5/2.6 integration.
 * Andras Toth for mknod rdev issue.
 * Michael Fischer for finding the problem with inode inconsistency.
 * Some code bodily lifted from JFFS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 *
 * This is the file system front-end to YAFFS that hooks it up to
 * the VFS.
 *
 * Special notes:
 * >> 2.4: sb->u.generic_sbp points to the yaffs_Device associated with
 *         this superblock
 * >> 2.6: sb->s_fs_info  points to the yaffs_Device associated with this
 *         superblock
 * >> inode->u.generic_ip points to the associated yaffs_Object.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10))
#define YAFFS_COMPILE_BACKGROUND
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
#define YAFFS_COMPILE_EXPORTFS
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
#include <linux/config.h>
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ctype.h>

#ifdef YAFFS_COMPILE_EXPORTFS
#include <linux/exportfs.h>
#endif

#ifdef YAFFS_COMPILE_BACKGROUND
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#endif

#include <asm/div64.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))

#include <linux/statfs.h>

#define UnlockPage(p) unlock_page(p)
#define Page_Uptodate(page)	test_bit(PG_uptodate, &(page)->flags)

/* FIXME: use sb->s_id instead ? */
#define yaffs_devname(sb, buf)	bdevname(sb->s_bdev, buf)

#else

#include <linux/locks.h>
#define	BDEVNAME_SIZE		0
#define	yaffs_devname(sb, buf)	kdevname(sb->s_dev)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0))
/* added NCB 26/5/2006 for 2.4.25-vrs2-tcl1 kernel */
#define __user
#endif

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26))
#define YPROC_ROOT  (&proc_root)
#else
#define YPROC_ROOT  NULL
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
#define WRITE_SIZE_STR "writesize"
#define WRITE_SIZE(mtd) ((mtd)->writesize)
#else
#define WRITE_SIZE_STR "oobblock"
#define WRITE_SIZE(mtd) ((mtd)->oobblock)
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 27))
#define YAFFS_USE_WRITE_BEGIN_END 1
#else
#define YAFFS_USE_WRITE_BEGIN_END 0
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 28))
static uint32_t YCALCBLOCKS(uint64_t partition_size, uint32_t block_size)
{
	uint64_t result = partition_size;
	do_div(result, block_size);
	return (uint32_t)result;
}
#else
#define YCALCBLOCKS(s, b) ((s)/(b))
#endif

#include <linux/uaccess.h>
#include <linux/mtd/mtd.h>

#include "yportenv.h"
#include "yaffs_trace.h"
#include "yaffs_guts.h"

#include "yaffs_linux.h"

#include "yaffs_mtdif.h"
#include "yaffs_mtdif1.h"
#include "yaffs_mtdif2.h"

unsigned int yaffs_traceMask = YAFFS_TRACE_BAD_BLOCKS | YAFFS_TRACE_ALWAYS;
unsigned int yaffs_wr_attempts = YAFFS_WR_ATTEMPTS;
unsigned int yaffs_auto_checkpoint = 1;
unsigned int yaffs_gc_control = 1;

/* Module Parameters */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
module_param(yaffs_traceMask, uint, 0644);
module_param(yaffs_wr_attempts, uint, 0644);
module_param(yaffs_auto_checkpoint, uint, 0644);
module_param(yaffs_gc_control, uint, 0644);
#else
MODULE_PARM(yaffs_traceMask, "i");
MODULE_PARM(yaffs_wr_attempts, "i");
MODULE_PARM(yaffs_auto_checkpoint, "i");
MODULE_PARM(yaffs_gc_control, "i");
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25))
/* use iget and read_inode */
#define Y_IGET(sb, inum) iget((sb), (inum))
static void yaffs_read_inode(struct inode *inode);

#else
/* Call local equivalent */
#define YAFFS_USE_OWN_IGET
#define Y_IGET(sb, inum) yaffs_iget((sb), (inum))

static struct inode *yaffs_iget(struct super_block *sb, unsigned long ino);
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
#define yaffs_InodeToObjectLV(iptr) ((iptr)->i_private)
#else
#define yaffs_InodeToObjectLV(iptr) ((iptr)->u.generic_ip)
#endif

#define yaffs_InodeToObject(iptr) ((yaffs_Object *)(yaffs_InodeToObjectLV(iptr)))
#define yaffs_DentryToObject(dptr) yaffs_InodeToObject((dptr)->d_inode)

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
#define yaffs_SuperToDevice(sb)	((yaffs_Device *)sb->s_fs_info)
#else
#define yaffs_SuperToDevice(sb)	((yaffs_Device *)sb->u.generic_sbp)
#endif


#define update_dir_time(dir) do {\
			(dir)->i_ctime = (dir)->i_mtime = CURRENT_TIME; \
		} while(0)
		
static void yaffs_put_super(struct super_block *sb);

static ssize_t yaffs_file_write(struct file *f, const char *buf, size_t n,
				loff_t *pos);
static ssize_t yaffs_hold_space(struct file *f);
static void yaffs_release_space(struct file *f);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_file_flush(struct file *file, fl_owner_t id);
#else
static int yaffs_file_flush(struct file *file);
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34))
static int yaffs_sync_object(struct file *file, int datasync);
#else
static int yaffs_sync_object(struct file *file, struct dentry *dentry,
				int datasync);
#endif

static int yaffs_readdir(struct file *f, void *dirent, filldir_t filldir);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode,
			struct nameidata *n);
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry,
					struct nameidata *n);
#else
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode);
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry);
#endif
static int yaffs_link(struct dentry *old_dentry, struct inode *dir,
			struct dentry *dentry);
static int yaffs_unlink(struct inode *dir, struct dentry *dentry);
static int yaffs_symlink(struct inode *dir, struct dentry *dentry,
			const char *symname);
static int yaffs_mkdir(struct inode *dir, struct dentry *dentry, int mode);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
			dev_t dev);
#else
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
			int dev);
#endif
static int yaffs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry);
static int yaffs_setattr(struct dentry *dentry, struct iattr *attr);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_sync_fs(struct super_block *sb, int wait);
static void yaffs_write_super(struct super_block *sb);
#else
static int yaffs_sync_fs(struct super_block *sb);
static int yaffs_write_super(struct super_block *sb);
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_statfs(struct dentry *dentry, struct kstatfs *buf);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_statfs(struct super_block *sb, struct kstatfs *buf);
#else
static int yaffs_statfs(struct super_block *sb, struct statfs *buf);
#endif

#ifdef YAFFS_HAS_PUT_INODE
static void yaffs_put_inode(struct inode *inode);
#endif

static void yaffs_delete_inode(struct inode *);
static void yaffs_clear_inode(struct inode *);

static int yaffs_readpage(struct file *file, struct page *page);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_writepage(struct page *page, struct writeback_control *wbc);
#else
static int yaffs_writepage(struct page *page);
#endif


#if (YAFFS_USE_WRITE_BEGIN_END != 0)
static int yaffs_write_begin(struct file *filp, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata);
static int yaffs_write_end(struct file *filp, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *pg, void *fsdadata);
#else
static int yaffs_prepare_write(struct file *f, struct page *pg,
				unsigned offset, unsigned to);
static int yaffs_commit_write(struct file *f, struct page *pg, unsigned offset,
				unsigned to);

#endif

static int yaffs_readlink(struct dentry *dentry, char __user *buffer,
				int buflen);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13))
static void *yaffs_follow_link(struct dentry *dentry, struct nameidata *nd);
#else
static int yaffs_follow_link(struct dentry *dentry, struct nameidata *nd);
#endif
static loff_t yaffs_dir_llseek(struct file *file, loff_t offset, int origin);

static struct address_space_operations yaffs_file_address_operations = {
	.readpage = yaffs_readpage,
	.writepage = yaffs_writepage,
#if (YAFFS_USE_WRITE_BEGIN_END > 0)
	.write_begin = yaffs_write_begin,
	.write_end = yaffs_write_end,
#else
	.prepare_write = yaffs_prepare_write,
	.commit_write = yaffs_commit_write,
#endif
};

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
static const struct file_operations yaffs_file_operations = {
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap,
	.flush = yaffs_file_flush,
	.fsync = yaffs_sync_object,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
	.llseek = generic_file_llseek,
};

#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))

static const struct file_operations yaffs_file_operations = {
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap,
	.flush = yaffs_file_flush,
	.fsync = yaffs_sync_object,
	.sendfile = generic_file_sendfile,
};

#else

static const struct file_operations yaffs_file_operations = {
	.read = generic_file_read,
	.write = generic_file_write,
	.mmap = generic_file_mmap,
	.flush = yaffs_file_flush,
	.fsync = yaffs_sync_object,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
	.sendfile = generic_file_sendfile,
#endif
};
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
static void zero_user_segment(struct page *page, unsigned start, unsigned end)
{
	void * kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + start, 0, end - start);
	kunmap_atomic(kaddr, KM_USER0);
	flush_dcache_page(page);
}
#endif


static const struct inode_operations yaffs_file_inode_operations = {
	.setattr = yaffs_setattr,
};

static const struct inode_operations yaffs_symlink_inode_operations = {
	.readlink = yaffs_readlink,
	.follow_link = yaffs_follow_link,
	.setattr = yaffs_setattr,
};

static const struct inode_operations yaffs_dir_inode_operations = {
	.create = yaffs_create,
	.lookup = yaffs_lookup,
	.link = yaffs_link,
	.unlink = yaffs_unlink,
	.symlink = yaffs_symlink,
	.mkdir = yaffs_mkdir,
	.rmdir = yaffs_unlink,
	.mknod = yaffs_mknod,
	.rename = yaffs_rename,
	.setattr = yaffs_setattr,
};

static const struct file_operations yaffs_dir_operations = {
	.read = generic_read_dir,
	.readdir = yaffs_readdir,
	.fsync = yaffs_sync_object,
	.llseek = yaffs_dir_llseek,
};

static const struct super_operations yaffs_super_ops = {
	.statfs = yaffs_statfs,

#ifndef YAFFS_USE_OWN_IGET
	.read_inode = yaffs_read_inode,
#endif
#ifdef YAFFS_HAS_PUT_INODE
	.put_inode = yaffs_put_inode,
#endif
	.put_super = yaffs_put_super,
	.delete_inode = yaffs_delete_inode,
	.clear_inode = yaffs_clear_inode,
	.sync_fs = yaffs_sync_fs,
	.write_super = yaffs_write_super,
};

static unsigned yaffs_gc_control_callback(yaffs_Device *dev)
{
	return yaffs_gc_control;
}
                	                                                                                          	
static void yaffs_GrossLock(yaffs_Device *dev)
{
	T(YAFFS_TRACE_LOCK, (TSTR("yaffs locking %p\n"), current));
	down(&(yaffs_DeviceToContext(dev)->grossLock));
	T(YAFFS_TRACE_LOCK, (TSTR("yaffs locked %p\n"), current));
}

static void yaffs_GrossUnlock(yaffs_Device *dev)
{
	T(YAFFS_TRACE_LOCK, (TSTR("yaffs unlocking %p\n"), current));
	up(&(yaffs_DeviceToContext(dev)->grossLock));
}

#ifdef YAFFS_COMPILE_EXPORTFS

static struct inode *
yaffs2_nfs_get_inode(struct super_block *sb, uint64_t ino, uint32_t generation)
{
	return Y_IGET(sb, ino);
}

static struct dentry *
yaffs2_fh_to_dentry(struct super_block *sb, struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type, yaffs2_nfs_get_inode) ;
}

static struct dentry *
 yaffs2_fh_to_parent(struct super_block *sb, struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type, yaffs2_nfs_get_inode);
}

struct dentry *yaffs2_get_parent(struct dentry *dentry)
{

	struct super_block *sb = dentry->d_inode->i_sb;
	struct dentry *parent = ERR_PTR(-ENOENT);
	struct inode *inode;
	unsigned long parent_ino;
	yaffs_Object *d_obj;
	yaffs_Object *parent_obj;

	d_obj = yaffs_InodeToObject(dentry->d_inode);

	if (d_obj) {
		parent_obj = d_obj->parent;
		if (parent_obj) {
			parent_ino = yaffs_GetObjectInode(parent_obj);
			inode = Y_IGET(sb, parent_ino);

			if (IS_ERR(inode)) {
				parent = ERR_CAST(inode);
			} else {
				parent = d_obtain_alias(inode);
				if (!IS_ERR(parent)) {
					parent = ERR_PTR(-ENOMEM);
					iput(inode);
				}
			}
		}
	}

	return parent;
}

/* Just declare a zero structure as a NULL value implies
 * using the default functions of exportfs.
 */

static struct export_operations yaffs_export_ops =
{
	.fh_to_dentry = yaffs2_fh_to_dentry,
	.fh_to_parent = yaffs2_fh_to_parent,
	.get_parent = yaffs2_get_parent,
} ;

#endif

/*-----------------------------------------------------------------*/
/* Directory search context allows us to unlock access to yaffs during
 * filldir without causing problems with the directory being modified.
 * This is similar to the tried and tested mechanism used in yaffs direct.
 *
 * A search context iterates along a doubly linked list of siblings in the
 * directory. If the iterating object is deleted then this would corrupt
 * the list iteration, likely causing a crash. The search context avoids
 * this by using the removeObjectCallback to move the search context to the
 * next object before the object is deleted.
 *
 * Many readdirs (and thus seach conexts) may be alive simulateously so
 * each yaffs_Device has a list of these.
 *
 * A seach context lives for the duration of a readdir.
 *
 * All these functions must be called while yaffs is locked.
 */

struct yaffs_SearchContext {
	yaffs_Device *dev;
	yaffs_Object *dirObj;
	yaffs_Object *nextReturn;
	struct ylist_head others;
};

/*
 * yaffs_NewSearch() creates a new search context, initialises it and
 * adds it to the device's search context list.
 *
 * Called at start of readdir.
 */
static struct yaffs_SearchContext * yaffs_NewSearch(yaffs_Object *dir)
{
	yaffs_Device *dev = dir->myDev;
	struct yaffs_SearchContext *sc = YMALLOC(sizeof(struct yaffs_SearchContext));
	if(sc){
		sc->dirObj = dir;
		sc->dev = dev;
		if( ylist_empty(&sc->dirObj->variant.directoryVariant.children))
			sc->nextReturn = NULL;
		else
			sc->nextReturn = ylist_entry(
                                dir->variant.directoryVariant.children.next,
				yaffs_Object,siblings);
		YINIT_LIST_HEAD(&sc->others);
		ylist_add(&sc->others,&(yaffs_DeviceToContext(dev)->searchContexts));
	}
	return sc;
}

/*
 * yaffs_EndSearch() disposes of a search context and cleans up.
 */
static void yaffs_EndSearch(struct yaffs_SearchContext * sc)
{
	if(sc){
		ylist_del(&sc->others);
		YFREE(sc);
	}
}

/*
 * yaffs_SearchAdvance() moves a search context to the next object.
 * Called when the search iterates or when an object removal causes
 * the search context to be moved to the next object.
 */
static void yaffs_SearchAdvance(struct yaffs_SearchContext *sc)
{
        if(!sc)
                return;

        if( sc->nextReturn == NULL ||
                ylist_empty(&sc->dirObj->variant.directoryVariant.children))
                sc->nextReturn = NULL;
        else {
                struct ylist_head *next = sc->nextReturn->siblings.next;

                if( next == &sc->dirObj->variant.directoryVariant.children)
                        sc->nextReturn = NULL; /* end of list */
                else
                        sc->nextReturn = ylist_entry(next,yaffs_Object,siblings);
        }
}

/*
 * yaffs_RemoveObjectCallback() is called when an object is unlinked.
 * We check open search contexts and advance any which are currently
 * on the object being iterated.
 */
static void yaffs_RemoveObjectCallback(yaffs_Object *obj)
{

        struct ylist_head *i;
        struct yaffs_SearchContext *sc;
        struct ylist_head *search_contexts = &(yaffs_DeviceToContext(obj->myDev)->searchContexts);


        /* Iterate through the directory search contexts.
         * If any are currently on the object being removed, then advance
         * the search context to the next object to prevent a hanging pointer.
         */
         ylist_for_each(i, search_contexts) {
                if (i) {
                        sc = ylist_entry(i, struct yaffs_SearchContext,others);
                        if(sc->nextReturn == obj)
                                yaffs_SearchAdvance(sc);
                }
	}

}


/*-----------------------------------------------------------------*/

static int yaffs_readlink(struct dentry *dentry, char __user *buffer,
			int buflen)
{
	unsigned char *alias;
	int ret;

	yaffs_Device *dev = yaffs_DentryToObject(dentry)->myDev;

	yaffs_GrossLock(dev);

	alias = yaffs_GetSymlinkAlias(yaffs_DentryToObject(dentry));

	yaffs_GrossUnlock(dev);

	if (!alias)
		return -ENOMEM;

	ret = vfs_readlink(dentry, buffer, buflen, alias);
	kfree(alias);
	return ret;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13))
static void *yaffs_follow_link(struct dentry *dentry, struct nameidata *nd)
#else
static int yaffs_follow_link(struct dentry *dentry, struct nameidata *nd)
#endif
{
	unsigned char *alias;
	int ret;
	yaffs_Device *dev = yaffs_DentryToObject(dentry)->myDev;

	yaffs_GrossLock(dev);

	alias = yaffs_GetSymlinkAlias(yaffs_DentryToObject(dentry));

	yaffs_GrossUnlock(dev);

	if (!alias) {
		ret = -ENOMEM;
		goto out;
	}

	ret = vfs_follow_link(nd, alias);
	kfree(alias);
out:
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13))
	return ERR_PTR(ret);
#else
	return ret;
#endif
}

struct inode *yaffs_get_inode(struct super_block *sb, int mode, int dev,
				yaffs_Object *obj);

/*
 * Lookup is used to find objects in the fs
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))

static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry,
				struct nameidata *n)
#else
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry)
#endif
{
	yaffs_Object *obj;
	struct inode *inode = NULL;	/* NCB 2.5/2.6 needs NULL here */

	yaffs_Device *dev = yaffs_InodeToObject(dir)->myDev;

	if(current != yaffs_DeviceToContext(dev)->readdirProcess)
		yaffs_GrossLock(dev);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_lookup for %d:%s\n"),
		yaffs_InodeToObject(dir)->objectId, dentry->d_name.name));

	obj = yaffs_FindObjectByName(yaffs_InodeToObject(dir),
					dentry->d_name.name);

	obj = yaffs_GetEquivalentObject(obj);	/* in case it was a hardlink */

	/* Can't hold gross lock when calling yaffs_get_inode() */
	if(current != yaffs_DeviceToContext(dev)->readdirProcess)
		yaffs_GrossUnlock(dev);

	if (obj) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_lookup found %d\n"), obj->objectId));

		inode = yaffs_get_inode(dir->i_sb, obj->yst_mode, 0, obj);

		if (inode) {
			T(YAFFS_TRACE_OS,
				(TSTR("yaffs_loookup dentry \n")));
/* #if 0 asserted by NCB for 2.5/6 compatability - falls through to
 * d_add even if NULL inode */
#if 0
			/*dget(dentry); // try to solve directory bug */
			d_add(dentry, inode);

			/* return dentry; */
			return NULL;
#endif
		}

	} else {
		T(YAFFS_TRACE_OS,(TSTR("yaffs_lookup not found\n")));

	}

/* added NCB for 2.5/6 compatability - forces add even if inode is
 * NULL which creates dentry hash */
	d_add(dentry, inode);

	return NULL;
}


#ifdef YAFFS_HAS_PUT_INODE

/* For now put inode is just for debugging
 * Put inode is called when the inode **structure** is put.
 */
static void yaffs_put_inode(struct inode *inode)
{
	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_put_inode: ino %d, count %d\n"), (int)inode->i_ino,
		atomic_read(&inode->i_count)));

}
#endif

/* clear is called to tell the fs to release any per-inode data it holds */
static void yaffs_clear_inode(struct inode *inode)
{
	yaffs_Object *obj;
	yaffs_Device *dev;

	obj = yaffs_InodeToObject(inode);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_clear_inode: ino %d, count %d %s\n"), (int)inode->i_ino,
		atomic_read(&inode->i_count),
		obj ? "object exists" : "null object"));

	if (obj) {
		dev = obj->myDev;
		yaffs_GrossLock(dev);

		/* Clear the association between the inode and
		 * the yaffs_Object.
		 */
		obj->myInode = NULL;
		yaffs_InodeToObjectLV(inode) = NULL;

		/* If the object freeing was deferred, then the real
		 * free happens now.
		 * This should fix the inode inconsistency problem.
		 */

		yaffs_HandleDeferedFree(obj);

		yaffs_GrossUnlock(dev);
	}

}

/* delete is called when the link count is zero and the inode
 * is put (ie. nobody wants to know about it anymore, time to
 * delete the file).
 * NB Must call clear_inode()
 */
static void yaffs_delete_inode(struct inode *inode)
{
	yaffs_Object *obj = yaffs_InodeToObject(inode);
	yaffs_Device *dev;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_delete_inode: ino %d, count %d %s\n"), (int)inode->i_ino,
		atomic_read(&inode->i_count),
		obj ? "object exists" : "null object"));

	if (obj) {
		dev = obj->myDev;
		yaffs_GrossLock(dev);
		yaffs_DeleteObject(obj);
		yaffs_GrossUnlock(dev);
	}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13))
	truncate_inode_pages(&inode->i_data, 0);
#endif
	clear_inode(inode);
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_file_flush(struct file *file, fl_owner_t id)
#else
static int yaffs_file_flush(struct file *file)
#endif
{
	yaffs_Object *obj = yaffs_DentryToObject(file->f_dentry);

	yaffs_Device *dev = obj->myDev;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_file_flush object %d (%s)\n"), obj->objectId,
		obj->dirty ? "dirty" : "clean"));

	yaffs_GrossLock(dev);

	yaffs_FlushFile(obj, 1, 0);

	yaffs_GrossUnlock(dev);

	return 0;
}

static int yaffs_readpage_nolock(struct file *f, struct page *pg)
{
	/* Lifted from jffs2 */

	yaffs_Object *obj;
	unsigned char *pg_buf;
	int ret;

	yaffs_Device *dev;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_readpage_nolock at %08x, size %08x\n"),
		(unsigned)(pg->index << PAGE_CACHE_SHIFT),
		(unsigned)PAGE_CACHE_SIZE));

	obj = yaffs_DentryToObject(f->f_dentry);

	dev = obj->myDev;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
	BUG_ON(!PageLocked(pg));
#else
	if (!PageLocked(pg))
		PAGE_BUG(pg);
#endif

	pg_buf = kmap(pg);
	/* FIXME: Can kmap fail? */

	yaffs_GrossLock(dev);

	ret = yaffs_ReadDataFromFile(obj, pg_buf,
				pg->index << PAGE_CACHE_SHIFT,
				PAGE_CACHE_SIZE);

	yaffs_GrossUnlock(dev);

	if (ret >= 0)
		ret = 0;

	if (ret) {
		ClearPageUptodate(pg);
		SetPageError(pg);
	} else {
		SetPageUptodate(pg);
		ClearPageError(pg);
	}

	flush_dcache_page(pg);
	kunmap(pg);

	T(YAFFS_TRACE_OS, (TSTR("yaffs_readpage_nolock done\n")));
	return ret;
}

static int yaffs_readpage_unlock(struct file *f, struct page *pg)
{
	int ret = yaffs_readpage_nolock(f, pg);
	UnlockPage(pg);
	return ret;
}

static int yaffs_readpage(struct file *f, struct page *pg)
{
	int ret;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_readpage\n")));
	ret=yaffs_readpage_unlock(f, pg);
	T(YAFFS_TRACE_OS, (TSTR("yaffs_readpage done\n")));
	return ret;
}

/* writepage inspired by/stolen from smbfs */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_writepage(struct page *page, struct writeback_control *wbc)
#else
static int yaffs_writepage(struct page *page)
#endif
{
	struct address_space *mapping = page->mapping;
	struct inode *inode;
	unsigned long end_index;
	char *buffer;
	yaffs_Object *obj;
	int nWritten = 0;
	unsigned nBytes;
	loff_t i_size;

	if (!mapping)
		BUG();
	inode = mapping->host;
	if (!inode)
		BUG();
	i_size = i_size_read(inode);

	end_index = i_size >> PAGE_CACHE_SHIFT;

	if(page->index < end_index)
		nBytes = PAGE_CACHE_SIZE;
	else {
		nBytes = i_size & (PAGE_CACHE_SIZE -1);

		if (page->index > end_index || !nBytes) {
			T(YAFFS_TRACE_OS,
				(TSTR("yaffs_writepage at %08x, inode size = %08x!!!\n"),
				(unsigned)(page->index << PAGE_CACHE_SHIFT),
				(unsigned)inode->i_size));
			T(YAFFS_TRACE_OS,
				(TSTR("                -> don't care!!\n")));

			zero_user_segment(page,0,PAGE_CACHE_SIZE);
			set_page_writeback(page);
			unlock_page(page);
			end_page_writeback(page);
			return 0;
		}
	}

	if(nBytes != PAGE_CACHE_SIZE)
		zero_user_segment(page,nBytes,PAGE_CACHE_SIZE);

	get_page(page);

	buffer = kmap(page);

	obj = yaffs_InodeToObject(inode);
	yaffs_GrossLock(obj->myDev);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_writepage at %08x, size %08x\n"),
		(unsigned)(page->index << PAGE_CACHE_SHIFT), nBytes));
	T(YAFFS_TRACE_OS,
		(TSTR("writepag0: obj = %05x, ino = %05x\n"),
		(int)obj->variant.fileVariant.fileSize, (int)inode->i_size));

	nWritten = yaffs_WriteDataToFile(obj, buffer,
			page->index << PAGE_CACHE_SHIFT, nBytes, 0);

	T(YAFFS_TRACE_OS,
		(TSTR("writepag1: obj = %05x, ino = %05x\n"),
		(int)obj->variant.fileVariant.fileSize, (int)inode->i_size));

	yaffs_GrossUnlock(obj->myDev);

	kunmap(page);
	set_page_writeback(page);
	unlock_page(page);
	end_page_writeback(page);
	put_page(page);

	return (nWritten == nBytes) ? 0 : -ENOSPC;
}


#if (YAFFS_USE_WRITE_BEGIN_END > 0)
static int yaffs_write_begin(struct file *filp, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	struct page *pg = NULL;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	int ret = 0;
	int space_held = 0;

	/* Get a page */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
	pg = grab_cache_page_write_begin(mapping, index, flags);
#else
	pg = __grab_cache_page(mapping, index);
#endif

	*pagep = pg;
	if (!pg) {
		ret =  -ENOMEM;
		goto out;
	}
	T(YAFFS_TRACE_OS,
		(TSTR("start yaffs_write_begin index %d(%x) uptodate %d\n"),
		(int)index,(int)index,Page_Uptodate(pg) ? 1 : 0));

	/* Get fs space */
	space_held = yaffs_hold_space(filp);

	if (!space_held) {
		ret = -ENOSPC;
		goto out;
	}

	/* Update page if required */

	if (!Page_Uptodate(pg))
		ret = yaffs_readpage_nolock(filp, pg);

	if (ret)
		goto out;

	/* Happy path return */
	T(YAFFS_TRACE_OS, (TSTR("end yaffs_write_begin - ok\n")));

	return 0;

out:
	T(YAFFS_TRACE_OS,
		(TSTR("end yaffs_write_begin fail returning %d\n"), ret));
	if (space_held)
		yaffs_release_space(filp);
	if (pg) {
		unlock_page(pg);
		page_cache_release(pg);
	}
	return ret;
}

#else

static int yaffs_prepare_write(struct file *f, struct page *pg,
				unsigned offset, unsigned to)
{
	T(YAFFS_TRACE_OS, (TSTR("yaffs_prepair_write\n")));

	if (!Page_Uptodate(pg))
		return yaffs_readpage_nolock(f, pg);
	return 0;
}
#endif

#if (YAFFS_USE_WRITE_BEGIN_END > 0)
static int yaffs_write_end(struct file *filp, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *pg, void *fsdadata)
{
	int ret = 0;
	void *addr, *kva;
	uint32_t offset_into_page = pos & (PAGE_CACHE_SIZE - 1);

	kva = kmap(pg);
	addr = kva + offset_into_page;

	T(YAFFS_TRACE_OS,
		("yaffs_write_end addr %p pos %x nBytes %d\n",
		addr,(unsigned)pos, copied));

	ret = yaffs_file_write(filp, addr, copied, &pos);

	if (ret != copied) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_write_end not same size ret %d  copied %d\n"),
			ret, copied));
		SetPageError(pg);
	} else {
		/* Nothing */
	}

	kunmap(pg);

	yaffs_release_space(filp);
	unlock_page(pg);
	page_cache_release(pg);
	return ret;
}
#else

static int yaffs_commit_write(struct file *f, struct page *pg, unsigned offset,
				unsigned to)
{
	void *addr, *kva;

	loff_t pos = (((loff_t) pg->index) << PAGE_CACHE_SHIFT) + offset;
	int nBytes = to - offset;
	int nWritten;

	unsigned spos = pos;
	unsigned saddr;

	kva = kmap(pg);
	addr = kva + offset;

	saddr = (unsigned) addr;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_commit_write addr %x pos %x nBytes %d\n"),
		saddr, spos, nBytes));

	nWritten = yaffs_file_write(f, addr, nBytes, &pos);

	if (nWritten != nBytes) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_commit_write not same size nWritten %d  nBytes %d\n"),
			nWritten, nBytes));
		SetPageError(pg);
	} else {
		/* Nothing */
	}

	kunmap(pg);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_commit_write returning %d\n"),
		nWritten == nBytes ? 0 : nWritten));

	return nWritten == nBytes ? 0 : nWritten;
}
#endif


static void yaffs_FillInodeFromObject(struct inode *inode, yaffs_Object *obj)
{
	if (inode && obj) {


		/* Check mode against the variant type and attempt to repair if broken. */
		__u32 mode = obj->yst_mode;
		switch (obj->variantType) {
		case YAFFS_OBJECT_TYPE_FILE:
			if (!S_ISREG(mode)) {
				obj->yst_mode &= ~S_IFMT;
				obj->yst_mode |= S_IFREG;
			}

			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
			if (!S_ISLNK(mode)) {
				obj->yst_mode &= ~S_IFMT;
				obj->yst_mode |= S_IFLNK;
			}

			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			if (!S_ISDIR(mode)) {
				obj->yst_mode &= ~S_IFMT;
				obj->yst_mode |= S_IFDIR;
			}

			break;
		case YAFFS_OBJECT_TYPE_UNKNOWN:
		case YAFFS_OBJECT_TYPE_HARDLINK:
		case YAFFS_OBJECT_TYPE_SPECIAL:
		default:
			/* TODO? */
			break;
		}

		inode->i_flags |= S_NOATIME;

		inode->i_ino = obj->objectId;
		inode->i_mode = obj->yst_mode;
		inode->i_uid = obj->yst_uid;
		inode->i_gid = obj->yst_gid;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
		inode->i_blksize = inode->i_sb->s_blocksize;
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))

		inode->i_rdev = old_decode_dev(obj->yst_rdev);
		inode->i_atime.tv_sec = (time_t) (obj->yst_atime);
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime.tv_sec = (time_t) obj->yst_mtime;
		inode->i_mtime.tv_nsec = 0;
		inode->i_ctime.tv_sec = (time_t) obj->yst_ctime;
		inode->i_ctime.tv_nsec = 0;
#else
		inode->i_rdev = obj->yst_rdev;
		inode->i_atime = obj->yst_atime;
		inode->i_mtime = obj->yst_mtime;
		inode->i_ctime = obj->yst_ctime;
#endif
		inode->i_size = yaffs_GetObjectFileLength(obj);
		inode->i_blocks = (inode->i_size + 511) >> 9;

		inode->i_nlink = yaffs_GetObjectLinkCount(obj);

		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_FillInode mode %x uid %d gid %d size %d count %d\n"),
			inode->i_mode, inode->i_uid, inode->i_gid,
			(int)inode->i_size, atomic_read(&inode->i_count)));

		switch (obj->yst_mode & S_IFMT) {
		default:	/* fifo, device or socket */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
			init_special_inode(inode, obj->yst_mode,
					old_decode_dev(obj->yst_rdev));
#else
			init_special_inode(inode, obj->yst_mode,
					(dev_t) (obj->yst_rdev));
#endif
			break;
		case S_IFREG:	/* file */
			inode->i_op = &yaffs_file_inode_operations;
			inode->i_fop = &yaffs_file_operations;
			inode->i_mapping->a_ops =
				&yaffs_file_address_operations;
			break;
		case S_IFDIR:	/* directory */
			inode->i_op = &yaffs_dir_inode_operations;
			inode->i_fop = &yaffs_dir_operations;
			break;
		case S_IFLNK:	/* symlink */
			inode->i_op = &yaffs_symlink_inode_operations;
			break;
		}

		yaffs_InodeToObjectLV(inode) = obj;

		obj->myInode = inode;

	} else {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_FileInode invalid parameters\n")));
	}

}

struct inode *yaffs_get_inode(struct super_block *sb, int mode, int dev,
				yaffs_Object *obj)
{
	struct inode *inode;

	if (!sb) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_get_inode for NULL super_block!!\n")));
		return NULL;

	}

	if (!obj) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_get_inode for NULL object!!\n")));
		return NULL;

	}

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_get_inode for object %d\n"), obj->objectId));

	inode = Y_IGET(sb, obj->objectId);
	if (IS_ERR(inode))
		return NULL;

	/* NB Side effect: iget calls back to yaffs_read_inode(). */
	/* iget also increments the inode's i_count */
	/* NB You can't be holding grossLock or deadlock will happen! */

	return inode;
}

static ssize_t yaffs_file_write(struct file *f, const char *buf, size_t n,
				loff_t *pos)
{
	yaffs_Object *obj;
	int nWritten, ipos;
	struct inode *inode;
	yaffs_Device *dev;

	obj = yaffs_DentryToObject(f->f_dentry);

	dev = obj->myDev;

	yaffs_GrossLock(dev);

	inode = f->f_dentry->d_inode;

	if (!S_ISBLK(inode->i_mode) && f->f_flags & O_APPEND)
		ipos = inode->i_size;
	else
		ipos = *pos;

	if (!obj)
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_file_write: hey obj is null!\n")));
	else
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_file_write about to write writing %u(%x) bytes"
			"to object %d at %d(%x)\n"),
			(unsigned) n, (unsigned) n, obj->objectId, ipos,ipos));

	nWritten = yaffs_WriteDataToFile(obj, buf, ipos, n, 0);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_file_write: %d(%x) bytes written\n"),
		(unsigned )n,(unsigned)n));

	if (nWritten > 0) {
		ipos += nWritten;
		*pos = ipos;
		if (ipos > inode->i_size) {
			inode->i_size = ipos;
			inode->i_blocks = (ipos + 511) >> 9;

			T(YAFFS_TRACE_OS,
				(TSTR("yaffs_file_write size updated to %d bytes, "
				"%d blocks\n"),
				ipos, (int)(inode->i_blocks)));
		}

	}
	yaffs_GrossUnlock(dev);
	return (nWritten == 0) && (n > 0) ? -ENOSPC : nWritten;
}

/* Space holding and freeing is done to ensure we have space available for write_begin/end */
/* For now we just assume few parallel writes and check against a small number. */
/* Todo: need to do this with a counter to handle parallel reads better */

static ssize_t yaffs_hold_space(struct file *f)
{
	yaffs_Object *obj;
	yaffs_Device *dev;

	int nFreeChunks;


	obj = yaffs_DentryToObject(f->f_dentry);

	dev = obj->myDev;

	yaffs_GrossLock(dev);

	nFreeChunks = yaffs_GetNumberOfFreeChunks(dev);

	yaffs_GrossUnlock(dev);

	return (nFreeChunks > 20) ? 1 : 0;
}

static void yaffs_release_space(struct file *f)
{
	yaffs_Object *obj;
	yaffs_Device *dev;


	obj = yaffs_DentryToObject(f->f_dentry);

	dev = obj->myDev;

	yaffs_GrossLock(dev);


	yaffs_GrossUnlock(dev);
}


static loff_t yaffs_dir_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;

	lock_kernel();

	switch (origin){
	case 2:
		offset += i_size_read(file->f_path.dentry->d_inode);
		break;
	case 1:
		offset += file->f_pos;
	}
	retval = -EINVAL;

	if (offset >= 0){
		if (offset != file->f_pos)
			file->f_pos = offset;

		retval = offset;
	}
	unlock_kernel();
	return retval;
}


static int yaffs_readdir(struct file *f, void *dirent, filldir_t filldir)
{
	yaffs_Object *obj;
	yaffs_Device *dev;
        struct yaffs_SearchContext *sc;
	struct inode *inode = f->f_dentry->d_inode;
	unsigned long offset, curoffs;
	yaffs_Object *l;
        int retVal = 0;

	char name[YAFFS_MAX_NAME_LENGTH + 1];

	obj = yaffs_DentryToObject(f->f_dentry);
	dev = obj->myDev;

	yaffs_GrossLock(dev);

	yaffs_DeviceToContext(dev)->readdirProcess = current;

	offset = f->f_pos;

        sc = yaffs_NewSearch(obj);
        if(!sc){
                retVal = -ENOMEM;
                goto unlock_out;
        }

	T(YAFFS_TRACE_OS, (TSTR("yaffs_readdir: starting at %d\n"), (int)offset));

	if (offset == 0) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_readdir: entry . ino %d \n"),
			(int)inode->i_ino));
		yaffs_GrossUnlock(dev);
		if (filldir(dirent, ".", 1, offset, inode->i_ino, DT_DIR) < 0)
			goto out;
		yaffs_GrossLock(dev);
		offset++;
		f->f_pos++;
	}
	if (offset == 1) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_readdir: entry .. ino %d \n"),
			(int)f->f_dentry->d_parent->d_inode->i_ino));
		yaffs_GrossUnlock(dev);
		if (filldir(dirent, "..", 2, offset,
			f->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0)
			goto out;
		yaffs_GrossLock(dev);
		offset++;
		f->f_pos++;
	}

	curoffs = 1;

	/* If the directory has changed since the open or last call to
	   readdir, rewind to after the 2 canned entries. */
	if (f->f_version != inode->i_version) {
		offset = 2;
		f->f_pos = offset;
		f->f_version = inode->i_version;
	}

	while(sc->nextReturn){
		curoffs++;
                l = sc->nextReturn;
		if (curoffs >= offset) {
                        int this_inode = yaffs_GetObjectInode(l);
                        int this_type = yaffs_GetObjectType(l);

			yaffs_GetObjectName(l, name,
					    YAFFS_MAX_NAME_LENGTH + 1);
			T(YAFFS_TRACE_OS,
			  (TSTR("yaffs_readdir: %s inode %d\n"),
			  name, yaffs_GetObjectInode(l)));

                        yaffs_GrossUnlock(dev);

			if (filldir(dirent,
					name,
					strlen(name),
					offset,
					this_inode,
					this_type) < 0)
				goto out;

                        yaffs_GrossLock(dev);

			offset++;
			f->f_pos++;
		}
                yaffs_SearchAdvance(sc);
	}

unlock_out:
	yaffs_DeviceToContext(dev)->readdirProcess = NULL;

	yaffs_GrossUnlock(dev);
out:
        yaffs_EndSearch(sc);

	return retVal;
}



/*
 * File creation. Allocate an inode, and we're done..
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)
#define YCRED(x) x
#else
#define YCRED(x) (x->cred)
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
			dev_t rdev)
#else
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
			int rdev)
#endif
{
	struct inode *inode;

	yaffs_Object *obj = NULL;
	yaffs_Device *dev;

	yaffs_Object *parent = yaffs_InodeToObject(dir);

	int error = -ENOSPC;
	uid_t uid = YCRED(current)->fsuid;
	gid_t gid = (dir->i_mode & S_ISGID) ? dir->i_gid : YCRED(current)->fsgid;

	if ((dir->i_mode & S_ISGID) && S_ISDIR(mode))
		mode |= S_ISGID;

	if (parent) {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_mknod: parent object %d type %d\n"),
			parent->objectId, parent->variantType));
	} else {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_mknod: could not get parent object\n")));
		return -EPERM;
	}

	T(YAFFS_TRACE_OS, (TSTR("yaffs_mknod: making oject for %s, "
			"mode %x dev %x\n"),
			dentry->d_name.name, mode, rdev));

	dev = parent->myDev;

	yaffs_GrossLock(dev);

	switch (mode & S_IFMT) {
	default:
		/* Special (socket, fifo, device...) */
		T(YAFFS_TRACE_OS, (TSTR("yaffs_mknod: making special\n")));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
		obj = yaffs_MknodSpecial(parent, dentry->d_name.name, mode, uid,
				gid, old_encode_dev(rdev));
#else
		obj = yaffs_MknodSpecial(parent, dentry->d_name.name, mode, uid,
				gid, rdev);
#endif
		break;
	case S_IFREG:		/* file          */
		T(YAFFS_TRACE_OS, (TSTR("yaffs_mknod: making file\n")));
		obj = yaffs_MknodFile(parent, dentry->d_name.name, mode, uid,
				gid);
		break;
	case S_IFDIR:		/* directory */
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_mknod: making directory\n")));
		obj = yaffs_MknodDirectory(parent, dentry->d_name.name, mode,
					uid, gid);
		break;
	case S_IFLNK:		/* symlink */
		T(YAFFS_TRACE_OS, (TSTR("yaffs_mknod: making symlink\n")));
		obj = NULL;	/* Do we ever get here? */
		break;
	}

	/* Can not call yaffs_get_inode() with gross lock held */
	yaffs_GrossUnlock(dev);

	if (obj) {
		inode = yaffs_get_inode(dir->i_sb, mode, rdev, obj);
		d_instantiate(dentry, inode);
		update_dir_time(dir);
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_mknod created object %d count = %d\n"),
			obj->objectId, atomic_read(&inode->i_count)));
		error = 0;
		yaffs_FillInodeFromObject(dir,parent);
	} else {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_mknod failed making object\n")));
		error = -ENOMEM;
	}

	return error;
}

static int yaffs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retVal;
	T(YAFFS_TRACE_OS, (TSTR("yaffs_mkdir\n")));
	retVal = yaffs_mknod(dir, dentry, mode | S_IFDIR, 0);
	return retVal;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode,
			struct nameidata *n)
#else
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	T(YAFFS_TRACE_OS,(TSTR("yaffs_create\n")));
	return yaffs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int yaffs_unlink(struct inode *dir, struct dentry *dentry)
{
	int retVal;

	yaffs_Device *dev;
	yaffs_Object *obj;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_unlink %d:%s\n"),
		(int)(dir->i_ino),
		dentry->d_name.name));
	obj = yaffs_InodeToObject(dir);
	dev = obj->myDev;

	yaffs_GrossLock(dev);

	retVal = yaffs_Unlink(obj, dentry->d_name.name);

	if (retVal == YAFFS_OK) {
		dentry->d_inode->i_nlink--;
		dir->i_version++;
		yaffs_GrossUnlock(dev);
		mark_inode_dirty(dentry->d_inode);
		update_dir_time(dir);
		return 0;
	}
	yaffs_GrossUnlock(dev);
	return -ENOTEMPTY;
}

/*
 * Create a link...
 */
static int yaffs_link(struct dentry *old_dentry, struct inode *dir,
			struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	yaffs_Object *obj = NULL;
	yaffs_Object *link = NULL;
	yaffs_Device *dev;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_link\n")));

	obj = yaffs_InodeToObject(inode);
	dev = obj->myDev;

	yaffs_GrossLock(dev);

	if (!S_ISDIR(inode->i_mode))		/* Don't link directories */
		link = yaffs_Link(yaffs_InodeToObject(dir), dentry->d_name.name,
			obj);

	if (link) {
		old_dentry->d_inode->i_nlink = yaffs_GetObjectLinkCount(obj);
		d_instantiate(dentry, old_dentry->d_inode);
		atomic_inc(&old_dentry->d_inode->i_count);
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_link link count %d i_count %d\n"),
			old_dentry->d_inode->i_nlink,
			atomic_read(&old_dentry->d_inode->i_count)));
	}

	yaffs_GrossUnlock(dev);

	if (link){
		update_dir_time(dir);
		return 0;
	}

	return -EPERM;
}

static int yaffs_symlink(struct inode *dir, struct dentry *dentry,
				const char *symname)
{
	yaffs_Object *obj;
	yaffs_Device *dev;
	uid_t uid = YCRED(current)->fsuid;
	gid_t gid = (dir->i_mode & S_ISGID) ? dir->i_gid : YCRED(current)->fsgid;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_symlink\n")));

	dev = yaffs_InodeToObject(dir)->myDev;
	yaffs_GrossLock(dev);
	obj = yaffs_MknodSymLink(yaffs_InodeToObject(dir), dentry->d_name.name,
				S_IFLNK | S_IRWXUGO, uid, gid, symname);
	yaffs_GrossUnlock(dev);

	if (obj) {
		struct inode *inode;

		inode = yaffs_get_inode(dir->i_sb, obj->yst_mode, 0, obj);
		d_instantiate(dentry, inode);
		update_dir_time(dir);
		T(YAFFS_TRACE_OS, (TSTR("symlink created OK\n")));
		return 0;
	} else {
		T(YAFFS_TRACE_OS, (TSTR("symlink not created\n")));
	}

	return -ENOMEM;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34))
static int yaffs_sync_object(struct file *file, int datasync)
#else
static int yaffs_sync_object(struct file *file, struct dentry *dentry,
				int datasync)
#endif
{

	yaffs_Object *obj;
	yaffs_Device *dev;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 34))
	struct dentry *dentry = file->f_path.dentry;
#endif

	obj = yaffs_DentryToObject(dentry);

	dev = obj->myDev;

	T(YAFFS_TRACE_OS | YAFFS_TRACE_SYNC,
		(TSTR("yaffs_sync_object\n")));
	yaffs_GrossLock(dev);
	yaffs_FlushFile(obj, 1, datasync);
	yaffs_GrossUnlock(dev);
	return 0;
}

/*
 * The VFS layer already does all the dentry stuff for rename.
 *
 * NB: POSIX says you can rename an object over an old object of the same name
 */
static int yaffs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	yaffs_Device *dev;
	int retVal = YAFFS_FAIL;
	yaffs_Object *target;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_rename\n")));
	dev = yaffs_InodeToObject(old_dir)->myDev;

	yaffs_GrossLock(dev);

	/* Check if the target is an existing directory that is not empty. */
	target = yaffs_FindObjectByName(yaffs_InodeToObject(new_dir),
				new_dentry->d_name.name);



	if (target && target->variantType == YAFFS_OBJECT_TYPE_DIRECTORY &&
		!ylist_empty(&target->variant.directoryVariant.children)) {

		T(YAFFS_TRACE_OS, (TSTR("target is non-empty dir\n")));

		retVal = YAFFS_FAIL;
	} else {
		/* Now does unlinking internally using shadowing mechanism */
		T(YAFFS_TRACE_OS, (TSTR("calling yaffs_RenameObject\n")));

		retVal = yaffs_RenameObject(yaffs_InodeToObject(old_dir),
				old_dentry->d_name.name,
				yaffs_InodeToObject(new_dir),
				new_dentry->d_name.name);
	}
	yaffs_GrossUnlock(dev);

	if (retVal == YAFFS_OK) {
		if (target) {
			new_dentry->d_inode->i_nlink--;
			mark_inode_dirty(new_dentry->d_inode);
		}
		
		update_dir_time(old_dir);
		if(old_dir != new_dir)
			update_dir_time(new_dir);
		return 0;
	} else {
		return -ENOTEMPTY;
	}
}

static int yaffs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error = 0;
	yaffs_Device *dev;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_setattr of object %d\n"),
		yaffs_InodeToObject(inode)->objectId));

	/* Fail if a requested resize >= 2GB */		
	if (attr->ia_valid & ATTR_SIZE &&
		(attr->ia_size >> 31))
		error = -EINVAL;

	if (error == 0)
		error = inode_change_ok(inode, attr);
	if (error == 0) {
		int result;
		if (!error){
			error = inode_setattr(inode, attr);
			T(YAFFS_TRACE_OS,(TSTR("inode_setattr called\n")));
			if (attr->ia_valid & ATTR_SIZE)
                        	truncate_inode_pages(&inode->i_data,attr->ia_size);
		}
		dev = yaffs_InodeToObject(inode)->myDev;
		if (attr->ia_valid & ATTR_SIZE){
			T(YAFFS_TRACE_OS,(TSTR("resize to %d(%x)\n"),
				(int)(attr->ia_size),(int)(attr->ia_size)));
		}
		yaffs_GrossLock(dev);
		result = yaffs_SetAttributes(yaffs_InodeToObject(inode), attr);
		if(result == YAFFS_OK) {
			error = 0;
		} else {
			error = -EPERM;
		}
		yaffs_GrossUnlock(dev);

	}

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_setattr done returning %d\n"),error));

	return error;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	yaffs_Device *dev = yaffs_DentryToObject(dentry)->myDev;
	struct super_block *sb = dentry->d_sb;
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);
#else
static int yaffs_statfs(struct super_block *sb, struct statfs *buf)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);
#endif

	T(YAFFS_TRACE_OS, (TSTR("yaffs_statfs\n")));

	yaffs_GrossLock(dev);

	buf->f_type = YAFFS_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_namelen = 255;

	if (dev->nDataBytesPerChunk & (dev->nDataBytesPerChunk - 1)) {
		/* Do this if chunk size is not a power of 2 */

		uint64_t bytesInDev;
		uint64_t bytesFree;

		bytesInDev = ((uint64_t)((dev->param.endBlock - dev->param.startBlock + 1))) *
			((uint64_t)(dev->param.nChunksPerBlock * dev->nDataBytesPerChunk));

		do_div(bytesInDev, sb->s_blocksize); /* bytesInDev becomes the number of blocks */
		buf->f_blocks = bytesInDev;

		bytesFree  = ((uint64_t)(yaffs_GetNumberOfFreeChunks(dev))) *
			((uint64_t)(dev->nDataBytesPerChunk));

		do_div(bytesFree, sb->s_blocksize);

		buf->f_bfree = bytesFree;

	} else if (sb->s_blocksize > dev->nDataBytesPerChunk) {

		buf->f_blocks =
			(dev->param.endBlock - dev->param.startBlock + 1) *
			dev->param.nChunksPerBlock /
			(sb->s_blocksize / dev->nDataBytesPerChunk);
		buf->f_bfree =
			yaffs_GetNumberOfFreeChunks(dev) /
			(sb->s_blocksize / dev->nDataBytesPerChunk);
	} else {
		buf->f_blocks =
			(dev->param.endBlock - dev->param.startBlock + 1) *
			dev->param.nChunksPerBlock *
			(dev->nDataBytesPerChunk / sb->s_blocksize);

		buf->f_bfree =
			yaffs_GetNumberOfFreeChunks(dev) *
			(dev->nDataBytesPerChunk / sb->s_blocksize);
	}

	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_bavail = buf->f_bfree;

	yaffs_GrossUnlock(dev);
	return 0;
}



static void yaffs_FlushInodes(struct super_block *sb)
{
	struct inode *iptr;
	yaffs_Object *obj;
	
	list_for_each_entry(iptr,&sb->s_inodes, i_sb_list){
		obj = yaffs_InodeToObject(iptr);
		if(obj){
			T(YAFFS_TRACE_OS, (TSTR("flushing obj %d\n"),
				obj->objectId));
			yaffs_FlushFile(obj,1,0);
		}
	}
}


static void yaffs_FlushSuperBlock(struct super_block *sb, int do_checkpoint)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);	
	if(!dev)
		return;
	
	yaffs_FlushInodes(sb);
	yaffs_UpdateDirtyDirectories(dev);
	yaffs_FlushEntireDeviceCache(dev);
	if(do_checkpoint)
		yaffs_CheckpointSave(dev);
}


static unsigned yaffs_bg_gc_urgency(yaffs_Device *dev)
{
	unsigned erasedChunks = dev->nErasedBlocks * dev->param.nChunksPerBlock;
	struct yaffs_LinuxContext *context = yaffs_DeviceToContext(dev);
	unsigned scatteredFree = 0; /* Free chunks not in an erased block */

	if(erasedChunks < dev->nFreeChunks)
		scatteredFree = (dev->nFreeChunks - erasedChunks);

	if(!context->bgRunning)
		return 0;
	else if(scatteredFree < (dev->param.nChunksPerBlock * 2))
		return 0;
	else if(erasedChunks > dev->nFreeChunks/2)
		return 0;
	else if(erasedChunks > dev->nFreeChunks/4)
		return 1;
	else
		return 2;
}

static int yaffs_do_sync_fs(struct super_block *sb,
				int request_checkpoint)
{

	yaffs_Device *dev = yaffs_SuperToDevice(sb);
	unsigned int oneshot_checkpoint = (yaffs_auto_checkpoint & 4);
	unsigned gc_urgent = yaffs_bg_gc_urgency(dev);
	int do_checkpoint;

	T(YAFFS_TRACE_OS | YAFFS_TRACE_SYNC | YAFFS_TRACE_BACKGROUND,
		(TSTR("yaffs_do_sync_fs: gc-urgency %d %s %s%s\n"),
		gc_urgent,
		sb->s_dirt ? "dirty" : "clean",
		request_checkpoint ? "checkpoint requested" : "no checkpoint",
		oneshot_checkpoint ? " one-shot" : "" ));

	yaffs_GrossLock(dev);
	do_checkpoint = ((request_checkpoint && !gc_urgent) ||
			oneshot_checkpoint) &&
			!dev->isCheckpointed;

	if (sb->s_dirt || do_checkpoint) {
		yaffs_FlushSuperBlock(sb, !dev->isCheckpointed && do_checkpoint);
		sb->s_dirt = 0;
		if(oneshot_checkpoint)
			yaffs_auto_checkpoint &= ~4;
	}
	yaffs_GrossUnlock(dev);

	return 0;
}

/*
 * yaffs background thread functions .
 * yaffs_BackgroundThread() the thread function
 * yaffs_BackgroundStart() launches the background thread.
 * yaffs_BackgroundStop() cleans up the background thread.
 *
 * NB: 
 * The thread should only run after the yaffs is initialised
 * The thread should be stopped before yaffs is unmounted.
 * The thread should not do any writing while the fs is in read only.
 */

#ifdef YAFFS_COMPILE_BACKGROUND

void yaffs_background_waker(unsigned long data)
{
	wake_up_process((struct task_struct *)data);
}

static int yaffs_BackgroundThread(void *data)
{
	yaffs_Device *dev = (yaffs_Device *)data;
	struct yaffs_LinuxContext *context = yaffs_DeviceToContext(dev);
	unsigned long now = jiffies;
	unsigned long next_dir_update = now;
	unsigned long next_gc = now;
	unsigned long expires;
	unsigned int urgency;

	int gcResult;
	struct timer_list timer;

	T(YAFFS_TRACE_BACKGROUND,
		(TSTR("yaffs_background starting for dev %p\n"),
		(void *)dev));

	set_freezable();

	while(context->bgRunning){
		T(YAFFS_TRACE_BACKGROUND,
			(TSTR("yaffs_background\n")));

		if(kthread_should_stop())
			break;

		if(try_to_freeze())
			continue;

		yaffs_GrossLock(dev);

		now = jiffies;

		if(time_after(now, next_dir_update)){
			yaffs_UpdateDirtyDirectories(dev);
			next_dir_update = now + HZ;
		}

		if(time_after(now,next_gc)){
			if(!dev->isCheckpointed){
				urgency = yaffs_bg_gc_urgency(dev);
				gcResult = yaffs_BackgroundGarbageCollect(dev, urgency);
				if(urgency > 1)
					next_gc = now + HZ/20+1;
				else if(urgency > 0)
					next_gc = now + HZ/10+1;
				else
					next_gc = now + HZ * 2;
			} else /*
				* gc not running so set to next_dir_update
				* to cut down on wake ups
				*/
				next_gc = next_dir_update;
		}
		yaffs_GrossUnlock(dev);
#if 1
		expires = next_dir_update;
		if (time_before(next_gc,expires))
			expires = next_gc;
		if(time_before(expires,now))
			expires = now + HZ;

		init_timer_on_stack(&timer);
		timer.expires = expires+1;
		timer.data = (unsigned long) current;
		timer.function = yaffs_background_waker;

                set_current_state(TASK_INTERRUPTIBLE);
		add_timer(&timer);
		schedule();
		del_timer_sync(&timer);
#else
		msleep(10);
#endif
	}

	return 0;
}

static int yaffs_BackgroundStart(yaffs_Device *dev)
{
	int retval = 0;
	struct yaffs_LinuxContext *context = yaffs_DeviceToContext(dev);

	context->bgRunning = 1;

	context->bgThread = kthread_run(yaffs_BackgroundThread,
	                        (void *)dev,"yaffs-bg");

	if(IS_ERR(context->bgThread)){
		retval = PTR_ERR(context->bgThread);
		context->bgThread = NULL;
		context->bgRunning = 0;
	}
	return retval;
}

static void yaffs_BackgroundStop(yaffs_Device *dev)
{
	struct yaffs_LinuxContext *ctxt = yaffs_DeviceToContext(dev);

	ctxt->bgRunning = 0;

	if( ctxt->bgThread){
		kthread_stop(ctxt->bgThread);
		ctxt->bgThread = NULL;
	}
}
#else
static int yaffs_BackgroundThread(void *data)
{
	return 0;
}

static int yaffs_BackgroundStart(yaffs_Device *dev)
{
	return 0;
}

static void yaffs_BackgroundStop(yaffs_Device *dev)
{
}
#endif


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static void yaffs_write_super(struct super_block *sb)
#else
static int yaffs_write_super(struct super_block *sb)
#endif
{
	unsigned request_checkpoint = (yaffs_auto_checkpoint >= 2);

	T(YAFFS_TRACE_OS | YAFFS_TRACE_SYNC | YAFFS_TRACE_BACKGROUND,
		(TSTR("yaffs_write_super%s\n"),
		request_checkpoint ? " checkpt" : ""));

	yaffs_do_sync_fs(sb, request_checkpoint);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
	return 0;
#endif
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_sync_fs(struct super_block *sb, int wait)
#else
static int yaffs_sync_fs(struct super_block *sb)
#endif
{
	unsigned request_checkpoint = (yaffs_auto_checkpoint >= 1);

	T(YAFFS_TRACE_OS | YAFFS_TRACE_SYNC,
		(TSTR("yaffs_sync_fs%s\n"),
		request_checkpoint ? " checkpt" : ""));

	yaffs_do_sync_fs(sb, request_checkpoint);

	return 0;
}

#ifdef YAFFS_USE_OWN_IGET

static struct inode *yaffs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	yaffs_Object *obj;
	yaffs_Device *dev = yaffs_SuperToDevice(sb);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_iget for %lu\n"), ino));

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	/* NB This is called as a side effect of other functions, but
	 * we had to release the lock to prevent deadlocks, so
	 * need to lock again.
	 */

	yaffs_GrossLock(dev);

	obj = yaffs_FindObjectByNumber(dev, inode->i_ino);

	yaffs_FillInodeFromObject(inode, obj);

	yaffs_GrossUnlock(dev);

	unlock_new_inode(inode);
	return inode;
}

#else

static void yaffs_read_inode(struct inode *inode)
{
	/* NB This is called as a side effect of other functions, but
	 * we had to release the lock to prevent deadlocks, so
	 * need to lock again.
	 */

	yaffs_Object *obj;
	yaffs_Device *dev = yaffs_SuperToDevice(inode->i_sb);

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_read_inode for %d\n"), (int)inode->i_ino));

	if(current != yaffs_DeviceToContext(dev)->readdirProcess)
		yaffs_GrossLock(dev);

	obj = yaffs_FindObjectByNumber(dev, inode->i_ino);

	yaffs_FillInodeFromObject(inode, obj);

	if(current != yaffs_DeviceToContext(dev)->readdirProcess)
		yaffs_GrossUnlock(dev);
}

#endif

static YLIST_HEAD(yaffs_context_list);
struct semaphore yaffs_context_lock;

#if 0 /* not used */
static int yaffs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	yaffs_Device    *dev = yaffs_SuperToDevice(sb);

	if (*flags & MS_RDONLY) {
		struct mtd_info *mtd = yaffs_SuperToDevice(sb)->genericDevice;

		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_remount_fs: %s: RO\n"), dev->name));

		yaffs_GrossLock(dev);

		yaffs_FlushSuperBlock(sb,1);

		if (mtd->sync)
			mtd->sync(mtd);

		yaffs_GrossUnlock(dev);
	} else {
		T(YAFFS_TRACE_OS,
			(TSTR("yaffs_remount_fs: %s: RW\n"), dev->name));
	}

	return 0;
}
#endif

static void yaffs_put_super(struct super_block *sb)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);

	T(YAFFS_TRACE_OS, (TSTR("yaffs_put_super\n")));

	T(YAFFS_TRACE_OS | YAFFS_TRACE_BACKGROUND,
		(TSTR("Shutting down yaffs background thread\n")));
	yaffs_BackgroundStop(dev);
	T(YAFFS_TRACE_OS | YAFFS_TRACE_BACKGROUND,
		(TSTR("yaffs background thread shut down\n")));

	yaffs_GrossLock(dev);

	yaffs_FlushSuperBlock(sb,1);

	if (yaffs_DeviceToContext(dev)->putSuperFunc)
		yaffs_DeviceToContext(dev)->putSuperFunc(sb);


	yaffs_Deinitialise(dev);

	yaffs_GrossUnlock(dev);

	down(&yaffs_context_lock);
	ylist_del_init(&(yaffs_DeviceToContext(dev)->contextList));
	up(&yaffs_context_lock);

	if (yaffs_DeviceToContext(dev)->spareBuffer) {
		YFREE(yaffs_DeviceToContext(dev)->spareBuffer);
		yaffs_DeviceToContext(dev)->spareBuffer = NULL;
	}

	kfree(dev);
}


static void yaffs_MTDPutSuper(struct super_block *sb)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(yaffs_SuperToDevice(sb))->mtd;

	if (mtd->sync)
		mtd->sync(mtd);

	put_mtd_device(mtd);
}


static void yaffs_MarkSuperBlockDirty(yaffs_Device *dev)
{
	struct super_block *sb = yaffs_DeviceToContext(dev)->superBlock;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_MarkSuperBlockDirty() sb = %p\n"), sb));
	if (sb)
		sb->s_dirt = 1;
}

typedef struct {
	int inband_tags;
	int skip_checkpoint_read;
	int skip_checkpoint_write;
	int no_cache;
	int tags_ecc_on;
	int tags_ecc_overridden;
	int lazy_loading_enabled;
	int lazy_loading_overridden;
	int empty_lost_and_found;
	int empty_lost_and_found_overridden;
} yaffs_options;

#define MAX_OPT_LEN 30
static int yaffs_parse_options(yaffs_options *options, const char *options_str)
{
	char cur_opt[MAX_OPT_LEN + 1];
	int p;
	int error = 0;

	/* Parse through the options which is a comma seperated list */

	while (options_str && *options_str && !error) {
		memset(cur_opt, 0, MAX_OPT_LEN + 1);
		p = 0;

		while(*options_str == ',')
			options_str++;

		while (*options_str && *options_str != ',') {
			if (p < MAX_OPT_LEN) {
				cur_opt[p] = *options_str;
				p++;
			}
			options_str++;
		}

		if (!strcmp(cur_opt, "inband-tags"))
			options->inband_tags = 1;
		else if (!strcmp(cur_opt, "tags-ecc-off")){
			options->tags_ecc_on = 0;
			options->tags_ecc_overridden=1;
		} else if (!strcmp(cur_opt, "tags-ecc-on")){
			options->tags_ecc_on = 1;
			options->tags_ecc_overridden = 1;
		} else if (!strcmp(cur_opt, "lazy-loading-off")){
			options->lazy_loading_enabled = 0;
			options->lazy_loading_overridden=1;
		} else if (!strcmp(cur_opt, "lazy-loading-on")){
			options->lazy_loading_enabled = 1;
			options->lazy_loading_overridden = 1;
		} else if (!strcmp(cur_opt, "empty-lost-and-found-off")){
			options->empty_lost_and_found = 0;
			options->empty_lost_and_found_overridden=1;
		} else if (!strcmp(cur_opt, "empty-lost-and-found-on")){
			options->empty_lost_and_found = 1;
			options->empty_lost_and_found_overridden=1;
		} else if (!strcmp(cur_opt, "no-cache"))
			options->no_cache = 1;
		else if (!strcmp(cur_opt, "no-checkpoint-read"))
			options->skip_checkpoint_read = 1;
		else if (!strcmp(cur_opt, "no-checkpoint-write"))
			options->skip_checkpoint_write = 1;
		else if (!strcmp(cur_opt, "no-checkpoint")) {
			options->skip_checkpoint_read = 1;
			options->skip_checkpoint_write = 1;
		} else {
			printk(KERN_INFO "yaffs: Bad mount option \"%s\"\n",
					cur_opt);
			error = 1;
		}
	}

	return error;
}

static struct super_block *yaffs_internal_read_super(int yaffsVersion,
						struct super_block *sb,
						void *data, int silent)
{
	int nBlocks;
	struct inode *inode = NULL;
	struct dentry *root;
	yaffs_Device *dev = 0;
	char devname_buf[BDEVNAME_SIZE + 1];
	struct mtd_info *mtd;
	int err;
	char *data_str = (char *)data;
	struct yaffs_LinuxContext *context = NULL;
	yaffs_DeviceParam *param;

	yaffs_options options;

	sb->s_magic = YAFFS_MAGIC;
	sb->s_op = &yaffs_super_ops;
	sb->s_flags |= MS_NOATIME;

#ifdef YAFFS_COMPILE_EXPORTFS
	sb->s_export_op = &yaffs_export_ops;
#endif

	if (!sb)
		printk(KERN_INFO "yaffs: sb is NULL\n");
	else if (!sb->s_dev)
		printk(KERN_INFO "yaffs: sb->s_dev is NULL\n");
	else if (!yaffs_devname(sb, devname_buf))
		printk(KERN_INFO "yaffs: devname is NULL\n");
	else
		printk(KERN_INFO "yaffs: dev is %d name is \"%s\"\n",
		       sb->s_dev,
		       yaffs_devname(sb, devname_buf));

	if (!data_str)
		data_str = "";

	printk(KERN_INFO "yaffs: passed flags \"%s\"\n", data_str);

	memset(&options, 0, sizeof(options));

	if (yaffs_parse_options(&options, data_str)) {
		/* Option parsing failed */
		return NULL;
	}


	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;

	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_read_super: Using yaffs%d\n"), yaffsVersion));
	T(YAFFS_TRACE_OS,
		(TSTR("yaffs_read_super: block size %d\n"),
		(int)(sb->s_blocksize)));

	T(YAFFS_TRACE_ALWAYS,
		(TSTR("yaffs: Attempting MTD mount of %u.%u,\"%s\"\n"),
	       MAJOR(sb->s_dev), MINOR(sb->s_dev),
	       yaffs_devname(sb, devname_buf)));

	/* Check it's an mtd device..... */
	if (MAJOR(sb->s_dev) != MTD_BLOCK_MAJOR)
		return NULL;	/* This isn't an mtd device */

	/* Get the device */
	mtd = get_mtd_device(NULL, MINOR(sb->s_dev));
	if (!mtd) {
		T(YAFFS_TRACE_ALWAYS,
			(TSTR("yaffs: MTD device #%u doesn't appear to exist\n"),
			MINOR(sb->s_dev)));
		return NULL;
	}
	/* Check it's NAND */
	if (mtd->type != MTD_NANDFLASH) {
		T(YAFFS_TRACE_ALWAYS,
			(TSTR("yaffs: MTD device is not NAND it's type %d\n"),
			mtd->type));
		return NULL;
	}

	T(YAFFS_TRACE_OS, (TSTR(" erase %p\n"), mtd->erase));
	T(YAFFS_TRACE_OS, (TSTR(" read %p\n"), mtd->read));
	T(YAFFS_TRACE_OS, (TSTR(" write %p\n"), mtd->write));
	T(YAFFS_TRACE_OS, (TSTR(" readoob %p\n"), mtd->read_oob));
	T(YAFFS_TRACE_OS, (TSTR(" writeoob %p\n"), mtd->write_oob));
	T(YAFFS_TRACE_OS, (TSTR(" block_isbad %p\n"), mtd->block_isbad));
	T(YAFFS_TRACE_OS, (TSTR(" block_markbad %p\n"), mtd->block_markbad));
	T(YAFFS_TRACE_OS, (TSTR(" %s %d\n"), WRITE_SIZE_STR, WRITE_SIZE(mtd)));
	T(YAFFS_TRACE_OS, (TSTR(" oobsize %d\n"), mtd->oobsize));
	T(YAFFS_TRACE_OS, (TSTR(" erasesize %d\n"), mtd->erasesize));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)
	T(YAFFS_TRACE_OS, (TSTR(" size %u\n"), mtd->size));
#else
	T(YAFFS_TRACE_OS, (TSTR(" size %lld\n"), mtd->size));
#endif

#ifdef CONFIG_YAFFS_AUTO_YAFFS2

	if (yaffsVersion == 1 && WRITE_SIZE(mtd) >= 2048) {
		T(YAFFS_TRACE_ALWAYS,
			(TSTR("yaffs: auto selecting yaffs2\n")));
		yaffsVersion = 2;
	}

	/* Added NCB 26/5/2006 for completeness */
	if (yaffsVersion == 2 && !options.inband_tags && WRITE_SIZE(mtd) == 512) {
		T(YAFFS_TRACE_ALWAYS,
			(TSTR("yaffs: auto selecting yaffs1\n")));
		yaffsVersion = 1;
	}

#endif

	if (yaffsVersion == 2) {
		/* Check for version 2 style functions */
		if (!mtd->erase ||
		    !mtd->block_isbad ||
		    !mtd->block_markbad ||
		    !mtd->read ||
		    !mtd->write ||
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
		    !mtd->read_oob || !mtd->write_oob) {
#else
		    !mtd->write_ecc ||
		    !mtd->read_ecc || !mtd->read_oob || !mtd->write_oob) {
#endif
			T(YAFFS_TRACE_ALWAYS,
			  (TSTR("yaffs: MTD device does not support required "
			   "functions\n")));
			return NULL;
		}

		if ((WRITE_SIZE(mtd) < YAFFS_MIN_YAFFS2_CHUNK_SIZE ||
		    mtd->oobsize < YAFFS_MIN_YAFFS2_SPARE_SIZE) &&
		    !options.inband_tags) {
			T(YAFFS_TRACE_ALWAYS,
			  (TSTR("yaffs: MTD device does not have the "
			   "right page sizes\n")));
			return NULL;
		}
	} else {
		/* Check for V1 style functions */
		if (!mtd->erase ||
		    !mtd->read ||
		    !mtd->write ||
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
		    !mtd->read_oob || !mtd->write_oob) {
#else
		    !mtd->write_ecc ||
		    !mtd->read_ecc || !mtd->read_oob || !mtd->write_oob) {
#endif
			T(YAFFS_TRACE_ALWAYS,
			  (TSTR("yaffs: MTD device does not support required "
			   "functions\n")));
			return NULL;
		}

		if (WRITE_SIZE(mtd) < YAFFS_BYTES_PER_CHUNK ||
		    mtd->oobsize != YAFFS_BYTES_PER_SPARE) {
			T(YAFFS_TRACE_ALWAYS,
			  (TSTR("yaffs: MTD device does not support have the "
			   "right page sizes\n")));
			return NULL;
		}
	}

	/* OK, so if we got here, we have an MTD that's NAND and looks
	 * like it has the right capabilities
	 * Set the yaffs_Device up for mtd
	 */

	dev = kmalloc(sizeof(yaffs_Device), GFP_KERNEL);
	context = kmalloc(sizeof(struct yaffs_LinuxContext),GFP_KERNEL);
	
	if(!dev || !context ){
		if(dev)
			kfree(dev);
		if(context)
			kfree(context);
		dev = NULL;
		context = NULL;
	}

	if (!dev) {
		/* Deep shit could not allocate device structure */
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR("yaffs_read_super: Failed trying to allocate "
		   "yaffs_Device. \n")));
		return NULL;
	}
	memset(dev, 0, sizeof(yaffs_Device));
	param = &(dev->param);

	memset(context,0,sizeof(struct yaffs_LinuxContext));
	dev->context = context;
	YINIT_LIST_HEAD(&(context->contextList));
	context->dev = dev;
	context->superBlock = sb;

	

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
	sb->s_fs_info = dev;
#else
	sb->u.generic_sbp = dev;
#endif
	
	yaffs_DeviceToContext(dev)->mtd = mtd;
	param->name = mtd->name;

	/* Set up the memory size parameters.... */

	nBlocks = YCALCBLOCKS(mtd->size, (YAFFS_CHUNKS_PER_BLOCK * YAFFS_BYTES_PER_CHUNK));

	param->startBlock = 0;
	param->endBlock = nBlocks - 1;
	param->nChunksPerBlock = YAFFS_CHUNKS_PER_BLOCK;
	param->totalBytesPerChunk = YAFFS_BYTES_PER_CHUNK;
	param->nReservedBlocks = 5;
	param->nShortOpCaches = (options.no_cache) ? 0 : 10;
	param->inbandTags = options.inband_tags;

#ifdef CONFIG_YAFFS_DISABLE_LAZY_LOAD
	param->disableLazyLoad = 1;
#endif
	if(options.lazy_loading_overridden)
		param->disableLazyLoad = !options.lazy_loading_enabled;

#ifdef CONFIG_YAFFS_DISABLE_TAGS_ECC
	param->noTagsECC = 1;
#endif

#ifdef CONFIG_YAFFS_DISABLE_BACKGROUND
#else
	param->deferDirectoryUpdate = 1;
#endif

	if(options.tags_ecc_overridden)
		param->noTagsECC = !options.tags_ecc_on;

#ifdef CONFIG_YAFFS_EMPTY_LOST_AND_FOUND
	param->emptyLostAndFound = 1;
#endif

#ifdef CONFIG_YAFFS_DISABLE_BLOCK_REFRESHING
	param->refreshPeriod = 0;
#else
	param->refreshPeriod = 500;
#endif

	if(options.empty_lost_and_found_overridden)
		param->emptyLostAndFound = options.empty_lost_and_found;

	/* ... and the functions. */
	if (yaffsVersion == 2) {
		param->writeChunkWithTagsToNAND =
		    nandmtd2_WriteChunkWithTagsToNAND;
		param->readChunkWithTagsFromNAND =
		    nandmtd2_ReadChunkWithTagsFromNAND;
		param->markNANDBlockBad = nandmtd2_MarkNANDBlockBad;
		param->queryNANDBlock = nandmtd2_QueryNANDBlock;
		yaffs_DeviceToContext(dev)->spareBuffer = YMALLOC(mtd->oobsize);
		param->isYaffs2 = 1;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
		param->totalBytesPerChunk = mtd->writesize;
		param->nChunksPerBlock = mtd->erasesize / mtd->writesize;
#else
		param->totalBytesPerChunk = mtd->oobblock;
		param->nChunksPerBlock = mtd->erasesize / mtd->oobblock;
#endif
		nBlocks = YCALCBLOCKS(mtd->size, mtd->erasesize);

		param->startBlock = 0;
		param->endBlock = nBlocks - 1;
	} else {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
		/* use the MTD interface in yaffs_mtdif1.c */
		param->writeChunkWithTagsToNAND =
			nandmtd1_WriteChunkWithTagsToNAND;
		param->readChunkWithTagsFromNAND =
			nandmtd1_ReadChunkWithTagsFromNAND;
		param->markNANDBlockBad = nandmtd1_MarkNANDBlockBad;
		param->queryNANDBlock = nandmtd1_QueryNANDBlock;
#else
		param->writeChunkToNAND = nandmtd_WriteChunkToNAND;
		param->readChunkFromNAND = nandmtd_ReadChunkFromNAND;
#endif
		param->isYaffs2 = 0;
	}
	/* ... and common functions */
	param->eraseBlockInNAND = nandmtd_EraseBlockInNAND;
	param->initialiseNAND = nandmtd_InitialiseNAND;

	yaffs_DeviceToContext(dev)->putSuperFunc = yaffs_MTDPutSuper;

	param->markSuperBlockDirty = yaffs_MarkSuperBlockDirty;
	param->gcControl = yaffs_gc_control_callback;

	yaffs_DeviceToContext(dev)->superBlock= sb;
	

#ifndef CONFIG_YAFFS_DOES_ECC
	param->useNANDECC = 1;
#endif

#ifdef CONFIG_YAFFS_DISABLE_WIDE_TNODES
	param->wideTnodesDisabled = 1;
#endif

	param->skipCheckpointRead = options.skip_checkpoint_read;
	param->skipCheckpointWrite = options.skip_checkpoint_write;

	/* we assume this is protected by lock_kernel() in mount/umount */
	down(&yaffs_context_lock);
	ylist_add_tail(&(yaffs_DeviceToContext(dev)->contextList), &yaffs_context_list);
	up(&yaffs_context_lock);

        /* Directory search handling...*/
        YINIT_LIST_HEAD(&(yaffs_DeviceToContext(dev)->searchContexts));
        param->removeObjectCallback = yaffs_RemoveObjectCallback;

	init_MUTEX(&(yaffs_DeviceToContext(dev)->grossLock));

	yaffs_GrossLock(dev);

	err = yaffs_GutsInitialise(dev);

	T(YAFFS_TRACE_OS,
	  (TSTR("yaffs_read_super: guts initialised %s\n"),
	   (err == YAFFS_OK) ? "OK" : "FAILED"));
	   
	if(err == YAFFS_OK)
		yaffs_BackgroundStart(dev);
		
	if(!context->bgThread)
		param->deferDirectoryUpdate = 0;


	/* Release lock before yaffs_get_inode() */
	yaffs_GrossUnlock(dev);

	/* Create root inode */
	if (err == YAFFS_OK)
		inode = yaffs_get_inode(sb, S_IFDIR | 0755, 0,
					yaffs_Root(dev));

	if (!inode)
		return NULL;

	inode->i_op = &yaffs_dir_inode_operations;
	inode->i_fop = &yaffs_dir_operations;

	T(YAFFS_TRACE_OS, (TSTR("yaffs_read_super: got root inode\n")));

	root = d_alloc_root(inode);

	T(YAFFS_TRACE_OS, (TSTR("yaffs_read_super: d_alloc_root done\n")));

	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	sb->s_dirt = !dev->isCheckpointed;
	T(YAFFS_TRACE_ALWAYS,
		(TSTR("yaffs_read_super: isCheckpointed %d\n"),
		dev->isCheckpointed));

	T(YAFFS_TRACE_OS, (TSTR("yaffs_read_super: done\n")));
	return sb;
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs_internal_read_super_mtd(struct super_block *sb, void *data,
					 int silent)
{
	return yaffs_internal_read_super(1, sb, data, silent) ? 0 : -EINVAL;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs_read_super(struct file_system_type *fs,
			    int flags, const char *dev_name,
			    void *data, struct vfsmount *mnt)
{

	return get_sb_bdev(fs, flags, dev_name, data,
			   yaffs_internal_read_super_mtd, mnt);
}
#else
static struct super_block *yaffs_read_super(struct file_system_type *fs,
					    int flags, const char *dev_name,
					    void *data)
{

	return get_sb_bdev(fs, flags, dev_name, data,
			   yaffs_internal_read_super_mtd);
}
#endif

static struct file_system_type yaffs_fs_type = {
	.owner = THIS_MODULE,
	.name = "yaffs",
	.get_sb = yaffs_read_super,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
#else
static struct super_block *yaffs_read_super(struct super_block *sb, void *data,
					    int silent)
{
	return yaffs_internal_read_super(1, sb, data, silent);
}

static DECLARE_FSTYPE(yaffs_fs_type, "yaffs", yaffs_read_super,
		      FS_REQUIRES_DEV);
#endif


#ifdef CONFIG_YAFFS_YAFFS2

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
static int yaffs2_internal_read_super_mtd(struct super_block *sb, void *data,
					  int silent)
{
	return yaffs_internal_read_super(2, sb, data, silent) ? 0 : -EINVAL;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 17))
static int yaffs2_read_super(struct file_system_type *fs,
			int flags, const char *dev_name, void *data,
			struct vfsmount *mnt)
{
	return get_sb_bdev(fs, flags, dev_name, data,
			yaffs2_internal_read_super_mtd, mnt);
}
#else
static struct super_block *yaffs2_read_super(struct file_system_type *fs,
					     int flags, const char *dev_name,
					     void *data)
{

	return get_sb_bdev(fs, flags, dev_name, data,
			   yaffs2_internal_read_super_mtd);
}
#endif

static struct file_system_type yaffs2_fs_type = {
	.owner = THIS_MODULE,
	.name = "yaffs2",
	.get_sb = yaffs2_read_super,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
#else
static struct super_block *yaffs2_read_super(struct super_block *sb,
					     void *data, int silent)
{
	return yaffs_internal_read_super(2, sb, data, silent);
}

static DECLARE_FSTYPE(yaffs2_fs_type, "yaffs2", yaffs2_read_super,
		      FS_REQUIRES_DEV);
#endif

#endif				/* CONFIG_YAFFS_YAFFS2 */

static struct proc_dir_entry *my_proc_entry;
static struct proc_dir_entry *debug_proc_entry;

static char *yaffs_dump_dev_part0(char *buf, yaffs_Device * dev)
{
	buf += sprintf(buf, "startBlock......... %d\n", dev->param.startBlock);
	buf += sprintf(buf, "endBlock........... %d\n", dev->param.endBlock);
	buf += sprintf(buf, "totalBytesPerChunk. %d\n", dev->param.totalBytesPerChunk);
	buf += sprintf(buf, "useNANDECC......... %d\n", dev->param.useNANDECC);
	buf += sprintf(buf, "noTagsECC.......... %d\n", dev->param.noTagsECC);
	buf += sprintf(buf, "isYaffs2........... %d\n", dev->param.isYaffs2);
	buf += sprintf(buf, "inbandTags......... %d\n", dev->param.inbandTags);
	buf += sprintf(buf, "emptyLostAndFound.. %d\n", dev->param.emptyLostAndFound);
	buf += sprintf(buf, "disableLazyLoad.... %d\n", dev->param.disableLazyLoad);
	buf += sprintf(buf, "refreshPeriod...... %d\n", dev->param.refreshPeriod);
	buf += sprintf(buf, "nShortOpCaches..... %d\n", dev->param.nShortOpCaches);
	buf += sprintf(buf, "nReservedBlocks.... %d\n", dev->param.nReservedBlocks);

	buf += sprintf(buf, "\n");

	return buf;
}


static char *yaffs_dump_dev_part1(char *buf, yaffs_Device * dev)
{
	buf += sprintf(buf, "nDataBytesPerChunk. %d\n", dev->nDataBytesPerChunk);
	buf += sprintf(buf, "chunkGroupBits..... %d\n", dev->chunkGroupBits);
	buf += sprintf(buf, "chunkGroupSize..... %d\n", dev->chunkGroupSize);
	buf += sprintf(buf, "nErasedBlocks...... %d\n", dev->nErasedBlocks);
	buf += sprintf(buf, "blocksInCheckpoint. %d\n", dev->blocksInCheckpoint);
	buf += sprintf(buf, "\n");
	buf += sprintf(buf, "nTnodesCreated..... %d\n", dev->nTnodesCreated);
	buf += sprintf(buf, "nFreeTnodes........ %d\n", dev->nFreeTnodes);
	buf += sprintf(buf, "nObjectsCreated.... %d\n", dev->nObjectsCreated);
	buf += sprintf(buf, "nFreeObjects....... %d\n", dev->nFreeObjects);
	buf += sprintf(buf, "nFreeChunks........ %d\n", dev->nFreeChunks);
	buf += sprintf(buf, "\n");
	buf += sprintf(buf, "nPageWrites........ %u\n", dev->nPageWrites);
	buf += sprintf(buf, "nPageReads......... %u\n", dev->nPageReads);
	buf += sprintf(buf, "nBlockErasures..... %u\n", dev->nBlockErasures);
	buf += sprintf(buf, "nGCCopies.......... %u\n", dev->nGCCopies);
	buf += sprintf(buf, "allGCs............. %u\n", dev->allGCs);
	buf += sprintf(buf, "passiveGCs......... %u\n", dev->passiveGCs);
	buf += sprintf(buf, "oldestDirtyGCs..... %u\n", dev->oldestDirtyGCs);
	buf += sprintf(buf, "backgroundGCs...... %u\n", dev->backgroundGCs);
	buf += sprintf(buf, "nRetriedWrites..... %u\n", dev->nRetriedWrites);
	buf += sprintf(buf, "nRetireBlocks...... %u\n", dev->nRetiredBlocks);
	buf += sprintf(buf, "eccFixed........... %u\n", dev->eccFixed);
	buf += sprintf(buf, "eccUnfixed......... %u\n", dev->eccUnfixed);
	buf += sprintf(buf, "tagsEccFixed....... %u\n", dev->tagsEccFixed);
	buf += sprintf(buf, "tagsEccUnfixed..... %u\n", dev->tagsEccUnfixed);
	buf += sprintf(buf, "cacheHits.......... %u\n", dev->cacheHits);
	buf += sprintf(buf, "nDeletedFiles...... %u\n", dev->nDeletedFiles);
	buf += sprintf(buf, "nUnlinkedFiles..... %u\n", dev->nUnlinkedFiles);
	buf += sprintf(buf, "refreshCount....... %u\n", dev->refreshCount);
	buf +=
	    sprintf(buf, "nBackgroudDeletions %u\n", dev->nBackgroundDeletions);

	return buf;
}

static int yaffs_proc_read(char *page,
			   char **start,
			   off_t offset, int count, int *eof, void *data)
{
	struct ylist_head *item;
	char *buf = page;
	int step = offset;
	int n = 0;

	/* Get proc_file_read() to step 'offset' by one on each sucessive call.
	 * We use 'offset' (*ppos) to indicate where we are in devList.
	 * This also assumes the user has posted a read buffer large
	 * enough to hold the complete output; but that's life in /proc.
	 */

	*(int *)start = 1;

	/* Print header first */
	if (step == 0)
		buf += sprintf(buf, "YAFFS built:" __DATE__ " " __TIME__"\n");
	else if (step == 1)
		buf += sprintf(buf,"\n");
	else {
		step-=2;
		
		down(&yaffs_context_lock);

		/* Locate and print the Nth entry.  Order N-squared but N is small. */
		ylist_for_each(item, &yaffs_context_list) {
			struct yaffs_LinuxContext *dc = ylist_entry(item, struct yaffs_LinuxContext, contextList);
			yaffs_Device *dev = dc->dev;

			if (n < (step & ~1)) {
				n+=2;
				continue;
			}
			if((step & 1)==0){
				buf += sprintf(buf, "\nDevice %d \"%s\"\n", n, dev->param.name);
				buf = yaffs_dump_dev_part0(buf, dev);
			} else
				buf = yaffs_dump_dev_part1(buf, dev);
			
			break;
		}
		up(&yaffs_context_lock);
	}

	return buf - page < count ? buf - page : count;
}

static int yaffs_stats_proc_read(char *page,
				char **start,
				off_t offset, int count, int *eof, void *data)
{
	struct ylist_head *item;
	char *buf = page;
	int n = 0;

	down(&yaffs_context_lock);

	/* Locate and print the Nth entry.  Order N-squared but N is small. */
	ylist_for_each(item, &yaffs_context_list) {
		struct yaffs_LinuxContext *dc = ylist_entry(item, struct yaffs_LinuxContext, contextList);
		yaffs_Device *dev = dc->dev;

		int erasedChunks;
		int nObjects;
		int nTnodes;

		erasedChunks = dev->nErasedBlocks * dev->param.nChunksPerBlock;
		nObjects = dev->nObjectsCreated -dev->nFreeObjects;
		nTnodes = dev->nTnodesCreated - dev->nFreeTnodes;
		
		
		buf += sprintf(buf,"%d, %d, %d, %u, %u, %d, %d\n",
				n, dev->nFreeChunks, erasedChunks,
				dev->backgroundGCs, dev->oldestDirtyGCs,
				nObjects, nTnodes);
	}
	up(&yaffs_context_lock);


	return buf - page < count ? buf - page : count;
}

/**
 * Set the verbosity of the warnings and error messages.
 *
 * Note that the names can only be a..z or _ with the current code.
 */

static struct {
	char *mask_name;
	unsigned mask_bitfield;
} mask_flags[] = {
	{"allocate", YAFFS_TRACE_ALLOCATE},
	{"always", YAFFS_TRACE_ALWAYS},
	{"background", YAFFS_TRACE_BACKGROUND},
	{"bad_blocks", YAFFS_TRACE_BAD_BLOCKS},
	{"buffers", YAFFS_TRACE_BUFFERS},
	{"bug", YAFFS_TRACE_BUG},
	{"checkpt", YAFFS_TRACE_CHECKPOINT},
	{"deletion", YAFFS_TRACE_DELETION},
	{"erase", YAFFS_TRACE_ERASE},
	{"error", YAFFS_TRACE_ERROR},
	{"gc_detail", YAFFS_TRACE_GC_DETAIL},
	{"gc", YAFFS_TRACE_GC},
	{"lock", YAFFS_TRACE_LOCK},
	{"mtd", YAFFS_TRACE_MTD},
	{"nandaccess", YAFFS_TRACE_NANDACCESS},
	{"os", YAFFS_TRACE_OS},
	{"scan_debug", YAFFS_TRACE_SCAN_DEBUG},
	{"scan", YAFFS_TRACE_SCAN},
	{"tracing", YAFFS_TRACE_TRACING},
	{"sync", YAFFS_TRACE_SYNC},
	{"write", YAFFS_TRACE_WRITE},

	{"verify", YAFFS_TRACE_VERIFY},
	{"verify_nand", YAFFS_TRACE_VERIFY_NAND},
	{"verify_full", YAFFS_TRACE_VERIFY_FULL},
	{"verify_all", YAFFS_TRACE_VERIFY_ALL},

	{"all", 0xffffffff},
	{"none", 0},
	{NULL, 0},
};

#define MAX_MASK_NAME_LENGTH 40
static int yaffs_proc_write_trace_options(struct file *file, const char *buf,
					 unsigned long count, void *data)
{
	unsigned rg = 0, mask_bitfield;
	char *end;
	char *mask_name;
	const char *x;
	char substring[MAX_MASK_NAME_LENGTH + 1];
	int i;
	int done = 0;
	int add, len = 0;
	int pos = 0;

	rg = yaffs_traceMask;

	while (!done && (pos < count)) {
		done = 1;
		while ((pos < count) && isspace(buf[pos]))
			pos++;

		switch (buf[pos]) {
		case '+':
		case '-':
		case '=':
			add = buf[pos];
			pos++;
			break;

		default:
			add = ' ';
			break;
		}
		mask_name = NULL;

		mask_bitfield = simple_strtoul(buf + pos, &end, 0);

		if (end > buf + pos) {
			mask_name = "numeral";
			len = end - (buf + pos);
			pos += len;
			done = 0;
		} else {
			for (x = buf + pos, i = 0;
			    (*x == '_' || (*x >= 'a' && *x <= 'z')) &&
			    i < MAX_MASK_NAME_LENGTH; x++, i++, pos++)
				substring[i] = *x;
			substring[i] = '\0';

			for (i = 0; mask_flags[i].mask_name != NULL; i++) {
				if (strcmp(substring, mask_flags[i].mask_name) == 0) {
					mask_name = mask_flags[i].mask_name;
					mask_bitfield = mask_flags[i].mask_bitfield;
					done = 0;
					break;
				}
			}
		}

		if (mask_name != NULL) {
			done = 0;
			switch (add) {
			case '-':
				rg &= ~mask_bitfield;
				break;
			case '+':
				rg |= mask_bitfield;
				break;
			case '=':
				rg = mask_bitfield;
				break;
			default:
				rg |= mask_bitfield;
				break;
			}
		}
	}

	yaffs_traceMask = rg | YAFFS_TRACE_ALWAYS;

	printk(KERN_DEBUG "new trace = 0x%08X\n", yaffs_traceMask);

	if (rg & YAFFS_TRACE_ALWAYS) {
		for (i = 0; mask_flags[i].mask_name != NULL; i++) {
			char flag;
			flag = ((rg & mask_flags[i].mask_bitfield) ==
				mask_flags[i].mask_bitfield) ? '+' : '-';
			printk(KERN_DEBUG "%c%s\n", flag, mask_flags[i].mask_name);
		}
	}

	return count;
}


static int yaffs_proc_write(struct file *file, const char *buf,
					 unsigned long count, void *data)
{
        return yaffs_proc_write_trace_options(file, buf, count, data);
}

/* Stuff to handle installation of file systems */
struct file_system_to_install {
	struct file_system_type *fst;
	int installed;
};

static struct file_system_to_install fs_to_install[] = {
	{&yaffs_fs_type, 0},
	{&yaffs2_fs_type, 0},
	{NULL, 0}
};

static int __init init_yaffs_fs(void)
{
	int error = 0;
	struct file_system_to_install *fsinst;

	T(YAFFS_TRACE_ALWAYS,
	  (TSTR("yaffs built " __DATE__ " " __TIME__ " Installing. \n")));

#ifdef CONFIG_YAFFS_ALWAYS_CHECK_CHUNK_ERASED
	T(YAFFS_TRACE_ALWAYS,
	  (TSTR(" \n\n\n\nYAFFS-WARNING CONFIG_YAFFS_ALWAYS_CHECK_CHUNK_ERASED selected.\n\n\n\n")));
#endif




	init_MUTEX(&yaffs_context_lock);

	/* Install the proc_fs entries */
	my_proc_entry = create_proc_entry("yaffs",
					       S_IRUGO | S_IFREG,
					       YPROC_ROOT);

	if (my_proc_entry) {
		my_proc_entry->write_proc = yaffs_proc_write;
		my_proc_entry->read_proc = yaffs_proc_read;
		my_proc_entry->data = NULL;
	} else
		return -ENOMEM;

	debug_proc_entry = create_proc_entry("yaffs_stats",
					       S_IRUGO | S_IFREG,
					       YPROC_ROOT);

	if (debug_proc_entry) {
		debug_proc_entry->write_proc = NULL;
		debug_proc_entry->read_proc = yaffs_stats_proc_read;
		debug_proc_entry->data = NULL;
	} else
		return -ENOMEM;

	/* Now add the file system entries */

	fsinst = fs_to_install;

	while (fsinst->fst && !error) {
		error = register_filesystem(fsinst->fst);
		if (!error)
			fsinst->installed = 1;
		fsinst++;
	}

	/* Any errors? uninstall  */
	if (error) {
		fsinst = fs_to_install;

		while (fsinst->fst) {
			if (fsinst->installed) {
				unregister_filesystem(fsinst->fst);
				fsinst->installed = 0;
			}
			fsinst++;
		}
	}

	return error;
}

static void __exit exit_yaffs_fs(void)
{

	struct file_system_to_install *fsinst;

	T(YAFFS_TRACE_ALWAYS,
		(TSTR("yaffs built " __DATE__ " " __TIME__ " removing. \n")));

	remove_proc_entry("yaffs", YPROC_ROOT);
	remove_proc_entry("yaffs_stats", YPROC_ROOT);

	fsinst = fs_to_install;

	while (fsinst->fst) {
		if (fsinst->installed) {
			unregister_filesystem(fsinst->fst);
			fsinst->installed = 0;
		}
		fsinst++;
	}
}

module_init(init_yaffs_fs)
module_exit(exit_yaffs_fs)

MODULE_DESCRIPTION("YAFFS2 - a NAND specific flash file system");
MODULE_AUTHOR("Charles Manning, Aleph One Ltd., 2002-2010");
MODULE_LICENSE("GPL");
