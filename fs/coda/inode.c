// SPDX-License-Identifier: GPL-2.0
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
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/pid_namespace.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/vmalloc.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"
#include "coda_cache.h"

#include "coda_int.h"

/* VFS super_block ops */
static void coda_evict_inode(struct inode *);
static void coda_put_super(struct super_block *);
static int coda_statfs(struct dentry *dentry, struct kstatfs *buf);

static struct kmem_cache * coda_inode_cachep;

static struct inode *coda_alloc_inode(struct super_block *sb)
{
	struct coda_inode_info *ei;
	ei = alloc_inode_sb(sb, coda_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	memset(&ei->c_fid, 0, sizeof(struct CodaFid));
	ei->c_flags = 0;
	ei->c_uid = GLOBAL_ROOT_UID;
	ei->c_cached_perm = 0;
	spin_lock_init(&ei->c_lock);
	return &ei->vfs_inode;
}

static void coda_free_inode(struct inode *inode)
{
	kmem_cache_free(coda_inode_cachep, ITOC(inode));
}

static void init_once(void *foo)
{
	struct coda_inode_info *ei = (struct coda_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

int __init coda_init_inodecache(void)
{
	coda_inode_cachep = kmem_cache_create("coda_inode_cache",
				sizeof(struct coda_inode_info), 0,
				SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
				init_once);
	if (coda_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void coda_destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(coda_inode_cachep);
}

static int coda_reconfigure(struct fs_context *fc)
{
	sync_filesystem(fc->root->d_sb);
	fc->sb_flags |= SB_NOATIME;
	return 0;
}

/* exported operations */
static const struct super_operations coda_super_operations =
{
	.alloc_inode	= coda_alloc_inode,
	.free_inode	= coda_free_inode,
	.evict_inode	= coda_evict_inode,
	.put_super	= coda_put_super,
	.statfs		= coda_statfs,
};

struct coda_fs_context {
	int	idx;
};

enum {
	Opt_fd,
};

static const struct fs_parameter_spec coda_param_specs[] = {
	fsparam_fd	("fd",	Opt_fd),
	{}
};

static int coda_parse_fd(struct fs_context *fc, int fd)
{
	struct coda_fs_context *ctx = fc->fs_private;
	struct fd f;
	struct inode *inode;
	int idx;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;
	inode = file_inode(f.file);
	if (!S_ISCHR(inode->i_mode) || imajor(inode) != CODA_PSDEV_MAJOR) {
		fdput(f);
		return invalf(fc, "code: Not coda psdev");
	}

	idx = iminor(inode);
	fdput(f);

	if (idx < 0 || idx >= MAX_CODADEVS)
		return invalf(fc, "coda: Bad minor number");
	ctx->idx = idx;
	return 0;
}

static int coda_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, coda_param_specs, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_fd:
		return coda_parse_fd(fc, result.uint_32);
	}

	return 0;
}

/*
 * Parse coda's binary mount data form.  We ignore any errors and go with index
 * 0 if we get one for backward compatibility.
 */
static int coda_parse_monolithic(struct fs_context *fc, void *_data)
{
	struct coda_mount_data *data = _data;

	if (!data)
		return invalf(fc, "coda: Bad mount data");

	if (data->version != CODA_MOUNT_VERSION)
		return invalf(fc, "coda: Bad mount version");

	coda_parse_fd(fc, data->fd);
	return 0;
}

static int coda_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct coda_fs_context *ctx = fc->fs_private;
	struct inode *root = NULL;
	struct venus_comm *vc;
	struct CodaFid fid;
	int error;

	infof(fc, "coda: device index: %i\n", ctx->idx);

	vc = &coda_comms[ctx->idx];
	mutex_lock(&vc->vc_mutex);

	if (!vc->vc_inuse) {
		errorf(fc, "coda: No pseudo device");
		error = -EINVAL;
		goto unlock_out;
	}

	if (vc->vc_sb) {
		errorf(fc, "coda: Device already mounted");
		error = -EBUSY;
		goto unlock_out;
	}

	vc->vc_sb = sb;
	mutex_unlock(&vc->vc_mutex);

	sb->s_fs_info = vc;
	sb->s_flags |= SB_NOATIME;
	sb->s_blocksize = 4096;	/* XXXXX  what do we put here?? */
	sb->s_blocksize_bits = 12;
	sb->s_magic = CODA_SUPER_MAGIC;
	sb->s_op = &coda_super_operations;
	sb->s_d_op = &coda_dentry_operations;
	sb->s_time_gran = 1;
	sb->s_time_min = S64_MIN;
	sb->s_time_max = S64_MAX;

	error = super_setup_bdi(sb);
	if (error)
		goto error;

	/* get root fid from Venus: this needs the root inode */
	error = venus_rootfid(sb, &fid);
	if ( error ) {
		pr_warn("%s: coda_get_rootfid failed with %d\n",
			__func__, error);
		goto error;
	}
	pr_info("%s: rootfid is %s\n", __func__, coda_f2s(&fid));
	
	/* make root inode */
        root = coda_cnode_make(&fid, sb);
        if (IS_ERR(root)) {
		error = PTR_ERR(root);
		pr_warn("Failure of coda_cnode_make for root: error %d\n",
			error);
		goto error;
	} 

	pr_info("%s: rootinode is %ld dev %s\n",
		__func__, root->i_ino, root->i_sb->s_id);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		error = -EINVAL;
		goto error;
	}
	return 0;

error:
	mutex_lock(&vc->vc_mutex);
	vc->vc_sb = NULL;
	sb->s_fs_info = NULL;
unlock_out:
	mutex_unlock(&vc->vc_mutex);
	return error;
}

static void coda_put_super(struct super_block *sb)
{
	struct venus_comm *vcp = coda_vcp(sb);
	mutex_lock(&vcp->vc_mutex);
	vcp->vc_sb = NULL;
	sb->s_fs_info = NULL;
	mutex_unlock(&vcp->vc_mutex);
	mutex_destroy(&vcp->vc_mutex);

	pr_info("Bye bye.\n");
}

static void coda_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	coda_cache_clear_inode(inode);
}

int coda_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int flags)
{
	int err = coda_revalidate_inode(d_inode(path->dentry));
	if (!err)
		generic_fillattr(&nop_mnt_idmap, request_mask,
				 d_inode(path->dentry), stat);
	return err;
}

int coda_setattr(struct mnt_idmap *idmap, struct dentry *de,
		 struct iattr *iattr)
{
	struct inode *inode = d_inode(de);
	struct coda_vattr vattr;
	int error;

	memset(&vattr, 0, sizeof(vattr)); 

	inode_set_ctime_current(inode);
	coda_iattr_to_vattr(iattr, &vattr);
	vattr.va_type = C_VNON; /* cannot set type */

	/* Venus is responsible for truncating the container-file!!! */
	error = venus_setattr(inode->i_sb, coda_i2f(inode), &vattr);

	if (!error) {
	        coda_vattr_to_iattr(inode, &vattr); 
		coda_cache_clear_inode(inode);
	}
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
	
	error = venus_statfs(dentry, buf);

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

static int coda_get_tree(struct fs_context *fc)
{
	if (task_active_pid_ns(current) != &init_pid_ns)
		return -EINVAL;

	return get_tree_nodev(fc, coda_fill_super);
}

static void coda_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations coda_context_ops = {
	.free		= coda_free_fc,
	.parse_param	= coda_parse_param,
	.parse_monolithic = coda_parse_monolithic,
	.get_tree	= coda_get_tree,
	.reconfigure	= coda_reconfigure,
};

static int coda_init_fs_context(struct fs_context *fc)
{
	struct coda_fs_context *ctx;

	ctx = kzalloc(sizeof(struct coda_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	fc->fs_private = ctx;
	fc->ops = &coda_context_ops;
	return 0;
}

struct file_system_type coda_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "coda",
	.init_fs_context = coda_init_fs_context,
	.parameters	= coda_param_specs,
	.kill_sb	= kill_anon_super,
	.fs_flags	= FS_BINARY_MOUNTDATA,
};
MODULE_ALIAS_FS("coda");

