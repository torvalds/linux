/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: super.c,v 1.110 2005/11/07 11:14:42 gleixner Exp $
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/jffs2.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include "compr.h"
#include "nodelist.h"

static void jffs2_put_super(struct super_block *);

static kmem_cache_t *jffs2_inode_cachep;

static struct inode *jffs2_alloc_inode(struct super_block *sb)
{
	struct jffs2_inode_info *ei;
	ei = (struct jffs2_inode_info *)kmem_cache_alloc(jffs2_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void jffs2_destroy_inode(struct inode *inode)
{
	kmem_cache_free(jffs2_inode_cachep, JFFS2_INODE_INFO(inode));
}

static void jffs2_i_init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct jffs2_inode_info *ei = (struct jffs2_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		init_MUTEX_LOCKED(&ei->sem);
		inode_init_once(&ei->vfs_inode);
	}
}

static int jffs2_sync_fs(struct super_block *sb, int wait)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	down(&c->alloc_sem);
	jffs2_flush_wbuf_pad(c);
	up(&c->alloc_sem);
	return 0;
}

static struct super_operations jffs2_super_operations =
{
	.alloc_inode =	jffs2_alloc_inode,
	.destroy_inode =jffs2_destroy_inode,
	.read_inode =	jffs2_read_inode,
	.put_super =	jffs2_put_super,
	.write_super =	jffs2_write_super,
	.statfs =	jffs2_statfs,
	.remount_fs =	jffs2_remount_fs,
	.clear_inode =	jffs2_clear_inode,
	.dirty_inode =	jffs2_dirty_inode,
	.sync_fs =	jffs2_sync_fs,
};

static int jffs2_sb_compare(struct super_block *sb, void *data)
{
	struct jffs2_sb_info *p = data;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	/* The superblocks are considered to be equivalent if the underlying MTD
	   device is the same one */
	if (c->mtd == p->mtd) {
		D1(printk(KERN_DEBUG "jffs2_sb_compare: match on device %d (\"%s\")\n", p->mtd->index, p->mtd->name));
		return 1;
	} else {
		D1(printk(KERN_DEBUG "jffs2_sb_compare: No match, device %d (\"%s\"), device %d (\"%s\")\n",
			  c->mtd->index, c->mtd->name, p->mtd->index, p->mtd->name));
		return 0;
	}
}

static int jffs2_sb_set(struct super_block *sb, void *data)
{
	struct jffs2_sb_info *p = data;

	/* For persistence of NFS exports etc. we use the same s_dev
	   each time we mount the device, don't just use an anonymous
	   device */
	sb->s_fs_info = p;
	p->os_priv = sb;
	sb->s_dev = MKDEV(MTD_BLOCK_MAJOR, p->mtd->index);

	return 0;
}

static struct super_block *jffs2_get_sb_mtd(struct file_system_type *fs_type,
					      int flags, const char *dev_name,
					      void *data, struct mtd_info *mtd)
{
	struct super_block *sb;
	struct jffs2_sb_info *c;
	int ret;

	c = kmalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);
	memset(c, 0, sizeof(*c));
	c->mtd = mtd;

	sb = sget(fs_type, jffs2_sb_compare, jffs2_sb_set, c);

	if (IS_ERR(sb))
		goto out_put;

	if (sb->s_root) {
		/* New mountpoint for JFFS2 which is already mounted */
		D1(printk(KERN_DEBUG "jffs2_get_sb_mtd(): Device %d (\"%s\") is already mounted\n",
			  mtd->index, mtd->name));
		goto out_put;
	}

	D1(printk(KERN_DEBUG "jffs2_get_sb_mtd(): New superblock for device %d (\"%s\")\n",
		  mtd->index, mtd->name));

	/* Initialize JFFS2 superblock locks, the further initialization will be
	 * done later */
	init_MUTEX(&c->alloc_sem);
	init_MUTEX(&c->erase_free_sem);
	init_waitqueue_head(&c->erase_wait);
	init_waitqueue_head(&c->inocache_wq);
	spin_lock_init(&c->erase_completion_lock);
	spin_lock_init(&c->inocache_lock);

	sb->s_op = &jffs2_super_operations;
	sb->s_flags = flags | MS_NOATIME;

	ret = jffs2_do_fill_super(sb, data, (flags&MS_VERBOSE)?1:0);

	if (ret) {
		/* Failure case... */
		up_write(&sb->s_umount);
		deactivate_super(sb);
		return ERR_PTR(ret);
	}

	sb->s_flags |= MS_ACTIVE;
	return sb;

 out_put:
	kfree(c);
	put_mtd_device(mtd);

	return sb;
}

static struct super_block *jffs2_get_sb_mtdnr(struct file_system_type *fs_type,
					      int flags, const char *dev_name,
					      void *data, int mtdnr)
{
	struct mtd_info *mtd;

	mtd = get_mtd_device(NULL, mtdnr);
	if (!mtd) {
		D1(printk(KERN_DEBUG "jffs2: MTD device #%u doesn't appear to exist\n", mtdnr));
		return ERR_PTR(-EINVAL);
	}

	return jffs2_get_sb_mtd(fs_type, flags, dev_name, data, mtd);
}

static struct super_block *jffs2_get_sb(struct file_system_type *fs_type,
					int flags, const char *dev_name,
					void *data)
{
	int err;
	struct nameidata nd;
	int mtdnr;

	if (!dev_name)
		return ERR_PTR(-EINVAL);

	D1(printk(KERN_DEBUG "jffs2_get_sb(): dev_name \"%s\"\n", dev_name));

	/* The preferred way of mounting in future; especially when
	   CONFIG_BLK_DEV is implemented - we specify the underlying
	   MTD device by number or by name, so that we don't require
	   block device support to be present in the kernel. */

	/* FIXME: How to do the root fs this way? */

	if (dev_name[0] == 'm' && dev_name[1] == 't' && dev_name[2] == 'd') {
		/* Probably mounting without the blkdev crap */
		if (dev_name[3] == ':') {
			struct mtd_info *mtd;

			/* Mount by MTD device name */
			D1(printk(KERN_DEBUG "jffs2_get_sb(): mtd:%%s, name \"%s\"\n", dev_name+4));
			for (mtdnr = 0; mtdnr < MAX_MTD_DEVICES; mtdnr++) {
				mtd = get_mtd_device(NULL, mtdnr);
				if (mtd) {
					if (!strcmp(mtd->name, dev_name+4))
						return jffs2_get_sb_mtd(fs_type, flags, dev_name, data, mtd);
					put_mtd_device(mtd);
				}
			}
			printk(KERN_NOTICE "jffs2_get_sb(): MTD device with name \"%s\" not found.\n", dev_name+4);
		} else if (isdigit(dev_name[3])) {
			/* Mount by MTD device number name */
			char *endptr;

			mtdnr = simple_strtoul(dev_name+3, &endptr, 0);
			if (!*endptr) {
				/* It was a valid number */
				D1(printk(KERN_DEBUG "jffs2_get_sb(): mtd%%d, mtdnr %d\n", mtdnr));
				return jffs2_get_sb_mtdnr(fs_type, flags, dev_name, data, mtdnr);
			}
		}
	}

	/* Try the old way - the hack where we allowed users to mount
	   /dev/mtdblock$(n) but didn't actually _use_ the blkdev */

	err = path_lookup(dev_name, LOOKUP_FOLLOW, &nd);

	D1(printk(KERN_DEBUG "jffs2_get_sb(): path_lookup() returned %d, inode %p\n",
		  err, nd.dentry->d_inode));

	if (err)
		return ERR_PTR(err);

	err = -EINVAL;

	if (!S_ISBLK(nd.dentry->d_inode->i_mode))
		goto out;

	if (nd.mnt->mnt_flags & MNT_NODEV) {
		err = -EACCES;
		goto out;
	}

	if (imajor(nd.dentry->d_inode) != MTD_BLOCK_MAJOR) {
		if (!(flags & MS_VERBOSE)) /* Yes I mean this. Strangely */
			printk(KERN_NOTICE "Attempt to mount non-MTD device \"%s\" as JFFS2\n",
			       dev_name);
		goto out;
	}

	mtdnr = iminor(nd.dentry->d_inode);
	path_release(&nd);

	return jffs2_get_sb_mtdnr(fs_type, flags, dev_name, data, mtdnr);

out:
	path_release(&nd);
	return ERR_PTR(err);
}

static void jffs2_put_super (struct super_block *sb)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	D2(printk(KERN_DEBUG "jffs2: jffs2_put_super()\n"));

	down(&c->alloc_sem);
	jffs2_flush_wbuf_pad(c);
	up(&c->alloc_sem);

	jffs2_sum_exit(c);

	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	if (jffs2_blocks_use_vmalloc(c))
		vfree(c->blocks);
	else
		kfree(c->blocks);
	jffs2_flash_cleanup(c);
	kfree(c->inocache_list);
	if (c->mtd->sync)
		c->mtd->sync(c->mtd);

	D1(printk(KERN_DEBUG "jffs2_put_super returning\n"));
}

static void jffs2_kill_sb(struct super_block *sb)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);
	if (!(sb->s_flags & MS_RDONLY))
		jffs2_stop_garbage_collect_thread(c);
	generic_shutdown_super(sb);
	put_mtd_device(c->mtd);
	kfree(c);
}

static struct file_system_type jffs2_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"jffs2",
	.get_sb =	jffs2_get_sb,
	.kill_sb =	jffs2_kill_sb,
};

static int __init init_jffs2_fs(void)
{
	int ret;

	printk(KERN_INFO "JFFS2 version 2.2."
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	       " (NAND)"
#endif
#ifdef CONFIG_JFFS2_SUMMARY
	       " (SUMMARY) "
#endif
	       " (C) 2001-2003 Red Hat, Inc.\n");

	jffs2_inode_cachep = kmem_cache_create("jffs2_i",
					     sizeof(struct jffs2_inode_info),
					     0, SLAB_RECLAIM_ACCOUNT,
					     jffs2_i_init_once, NULL);
	if (!jffs2_inode_cachep) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise inode cache\n");
		return -ENOMEM;
	}
	ret = jffs2_compressors_init();
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise compressors\n");
		goto out;
	}
	ret = jffs2_create_slab_caches();
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise slab caches\n");
		goto out_compressors;
	}
	ret = register_filesystem(&jffs2_fs_type);
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to register filesystem\n");
		goto out_slab;
	}
	return 0;

 out_slab:
	jffs2_destroy_slab_caches();
 out_compressors:
	jffs2_compressors_exit();
 out:
	kmem_cache_destroy(jffs2_inode_cachep);
	return ret;
}

static void __exit exit_jffs2_fs(void)
{
	unregister_filesystem(&jffs2_fs_type);
	jffs2_destroy_slab_caches();
	jffs2_compressors_exit();
	kmem_cache_destroy(jffs2_inode_cachep);
}

module_init(init_jffs2_fs);
module_exit(exit_jffs2_fs);

MODULE_DESCRIPTION("The Journalling Flash File System, v2");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL"); // Actually dual-licensed, but it doesn't matter for
		       // the sake of this tag. It's Free Software.
