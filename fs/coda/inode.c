/*
 * Super block/filesystem wide operations
 *
 * Copyright (C) 1996 Peter J. Braam <braam@maths.ox.ac.uk> and 
 * Michael Callahan <callahan@maths.ox.ac.uk> 
 * 
 * Rewritten for Linux 2.1.  Peter Braam <braam@cs.cmu.edu>
 * Copyright (C) Carnegie Mellon University
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/fs.h>
#include <linux/vmalloc.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_cache.h>

#include "coda_int.h"

/* VFS super_block ops */
static void coda_evict_inode(struct inode *);
static void coda_put_super(struct super_block *);
static int coda_statfs(struct dentry *dentry, struct kstatfs *buf);

static struct kmem_cache * coda_inode_cachep;

static struct inode *coda_alloc_inode(struct super_block *sb)
{
	struct coda_inode_info *ei;
	ei = (struct coda_inode_info *)kmem_cache_alloc(coda_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	memset(&ei->c_fid, 0, sizeof(struct CodaFid));
	ei->c_flags = 0;
	ei->c_uid = 0;
	ei->c_cached_perm = 0;
	return &ei->vfs_inode;
}

static void coda_destroy_inode(struct inode *inode)
{
	kmem_cache_free(coda_inode_cachep, ITOC(inode));
}

static void init_once(void *foo)
{
	struct coda_inode_info *ei = (struct coda_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

int coda_init_inodecache(void)
{
	coda_inode_cachep = kmem_cache_create("coda_inode_cache",
				sizeof(struct coda_inode_info),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (coda_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void coda_destroy_inodecache(void)
{
	kmem_cache_destroy(coda_inode_cachep);
}

static int coda_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NOATIME;
	return 0;
}

/* exported operations */
static const struct super_operations coda_super_operations =
{
	.alloc_inode	= coda_alloc_inode,
	.destroy_inode	= coda_destroy_inode,
	.evict_inode	= coda_evict_inode,
	.put_super	= coda_put_super,
	.statfs		= coda_statfs,
	.remount_fs	= coda_remount,
};

static int get_device_index(struct coda_mount_data *data)
{
	struct file *file;
	struct inode *inode;
	int idx;

	if(data == NULL) {
		printk("coda_read_super: Bad mount data\n");
		return -1;
	}

	if(data->version != CODA_MOUNT_VERSION) {
		printk("coda_read_super: Bad mount version\n");
		return -1;
	}

	file = fget(data->fd);
	inode = NULL;
	if(file)
		inode = file->f_path.dentry->d_inode;
	
	if(!inode || !S_ISCHR(inode->i_mode) ||
	   imajor(inode) != CODA_PSDEV_MAJOR) {
		if(file)
			fput(file);

		printk("coda_read_super: Bad file\n");
		return -1;
	}

	idx = iminor(inode);
	fput(file);

	if(idx < 0 || idx >= MAX_CODADEVS) {
		printk("coda_read_super: Bad minor number\n");
		return -1;
	}

	return idx;
}

static int coda_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root = NULL;
	struct venus_comm *vc = NULL;
	struct CodaFid fid;
	int error;
	int idx;

	lock_kernel();

	idx = get_device_index((struct coda_mount_data *) data);

	/* Ignore errors in data, for backward compatibility */
	if(idx == -1)
		idx = 0;
	
	printk(KERN_INFO "coda_read_super: device index: %i\n", idx);

	vc = &coda_comms[idx];
	if (!vc->vc_inuse) {
		printk("coda_read_super: No pseudo device\n");
		unlock_kernel();
		return -EINVAL;
	}

        if ( vc->vc_sb ) {
		printk("coda_read_super: Device already mounted\n");
		unlock_kernel();
		return -EBUSY;
	}

	error = bdi_setup_and_register(&vc->bdi, "coda", BDI_CAP_MAP_COPY);
	if (error)
		goto bdi_err;

	vc->vc_sb = sb;

	sb->s_fs_info = vc;
	sb->s_flags |= MS_NOATIME;
	sb->s_blocksize = 4096;	/* XXXXX  what do we put here?? */
	sb->s_blocksize_bits = 12;
	sb->s_magic = CODA_SUPER_MAGIC;
	sb->s_op = &coda_super_operations;
	sb->s_bdi = &vc->bdi;

	/* get root fid from Venus: this needs the root inode */
	error = venus_rootfid(sb, &fid);
	if ( error ) {
	        printk("coda_read_super: coda_get_rootfid failed with %d\n",
		       error);
		goto error;
	}
	printk("coda_read_super: rootfid is %s\n", coda_f2s(&fid));
	
	/* make root inode */
        error = coda_cnode_make(&root, &fid, sb);
        if ( error || !root ) {
	    printk("Failure of coda_cnode_make for root: error %d\n", error);
	    goto error;
	} 

	printk("coda_read_super: rootinode is %ld dev %s\n", 
	       root->i_ino, root->i_sb->s_id);
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root)
		goto error;
	unlock_kernel();
	return 0;

 error:
	bdi_destroy(&vc->bdi);
 bdi_err:
	if (root)
		iput(root);
	if (vc)
		vc->vc_sb = NULL;

	unlock_kernel();
	return -EINVAL;
}

static void coda_put_super(struct super_block *sb)
{
	bdi_destroy(&coda_vcp(sb)->bdi);
	coda_vcp(sb)->vc_sb = NULL;
	sb->s_fs_info = NULL;

	printk("Coda: Bye bye.\n");
}

static void coda_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);
	coda_cache_clear_inode(inode);
}

int coda_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	int err = coda_revalidate_inode(dentry);
	if (!err)
		generic_fillattr(dentry->d_inode, stat);
	return err;
}

int coda_setattr(struct dentry *de, struct iattr *iattr)
{
	struct inode *inode = de->d_inode;
	struct coda_vattr vattr;
	int error;

	lock_kernel();
	
	memset(&vattr, 0, sizeof(vattr)); 

	inode->i_ctime = CURRENT_TIME_SEC;
	coda_iattr_to_vattr(iattr, &vattr);
	vattr.va_type = C_VNON; /* cannot set type */

	/* Venus is responsible for truncating the container-file!!! */
	error = venus_setattr(inode->i_sb, coda_i2f(inode), &vattr);

	if ( !error ) {
	        coda_vattr_to_iattr(inode, &vattr); 
		coda_cache_clear_inode(inode);
	}

	unlock_kernel();

	return error;
}

const struct inode_operations coda_file_inode_operations = {
	.permission	= coda_permission,
	.getattr	= coda_getattr,
	.setattr	= coda_setattr,
};

static int coda_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int error;
	
	lock_kernel();

	error = venus_statfs(dentry, buf);

	unlock_kernel();

	if (error) {
		/* fake something like AFS does */
		buf->f_blocks = 9000000;
		buf->f_bfree  = 9000000;
		buf->f_bavail = 9000000;
		buf->f_files  = 9000000;
		buf->f_ffree  = 9000000;
	}

	/* and fill in the rest */
	buf->f_type = CODA_SUPER_MAGIC;
	buf->f_bsize = 4096;
	buf->f_namelen = CODA_MAXNAMLEN;

	return 0; 
}

/* init_coda: used by filesystems.c to register coda */

static int coda_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, coda_fill_super, mnt);
}

struct file_system_type coda_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "coda",
	.get_sb		= coda_get_sb,
	.kill_sb	= kill_anon_super,
	.fs_flags	= FS_BINARY_MOUNTDATA,
};

