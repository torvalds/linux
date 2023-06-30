// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2011 Novell Inc.
 */

#include <uapi/linux/magic.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/mount.h>
#include <linux/parser.h>
#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/posix_acl_xattr.h>
#include <linux/exportfs.h>
#include <linux/file.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include "overlayfs.h"

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Overlay filesystem");
MODULE_LICENSE("GPL");


struct ovl_dir_cache;

static bool ovl_redirect_dir_def = IS_ENABLED(CONFIG_OVERLAY_FS_REDIRECT_DIR);
module_param_named(redirect_dir, ovl_redirect_dir_def, bool, 0644);
MODULE_PARM_DESC(redirect_dir,
		 "Default to on or off for the redirect_dir feature");

static bool ovl_redirect_always_follow =
	IS_ENABLED(CONFIG_OVERLAY_FS_REDIRECT_ALWAYS_FOLLOW);
module_param_named(redirect_always_follow, ovl_redirect_always_follow,
		   bool, 0644);
MODULE_PARM_DESC(redirect_always_follow,
		 "Follow redirects even if redirect_dir feature is turned off");

static bool ovl_index_def = IS_ENABLED(CONFIG_OVERLAY_FS_INDEX);
module_param_named(index, ovl_index_def, bool, 0644);
MODULE_PARM_DESC(index,
		 "Default to on or off for the inodes index feature");

static bool ovl_nfs_export_def = IS_ENABLED(CONFIG_OVERLAY_FS_NFS_EXPORT);
module_param_named(nfs_export, ovl_nfs_export_def, bool, 0644);
MODULE_PARM_DESC(nfs_export,
		 "Default to on or off for the NFS export feature");

static bool ovl_xino_auto_def = IS_ENABLED(CONFIG_OVERLAY_FS_XINO_AUTO);
module_param_named(xino_auto, ovl_xino_auto_def, bool, 0644);
MODULE_PARM_DESC(xino_auto,
		 "Auto enable xino feature");

static bool ovl_metacopy_def = IS_ENABLED(CONFIG_OVERLAY_FS_METACOPY);
module_param_named(metacopy, ovl_metacopy_def, bool, 0644);
MODULE_PARM_DESC(metacopy,
		 "Default to on or off for the metadata only copy up feature");

static struct dentry *ovl_d_real(struct dentry *dentry,
				 const struct inode *inode)
{
	struct dentry *real = NULL, *lower;

	/* It's an overlay file */
	if (inode && d_inode(dentry) == inode)
		return dentry;

	if (!d_is_reg(dentry)) {
		if (!inode || inode == d_inode(dentry))
			return dentry;
		goto bug;
	}

	real = ovl_dentry_upper(dentry);
	if (real && (inode == d_inode(real)))
		return real;

	if (real && !inode && ovl_has_upperdata(d_inode(dentry)))
		return real;

	/*
	 * Best effort lazy lookup of lowerdata for !inode case to return
	 * the real lowerdata dentry.  The only current caller of d_real() with
	 * NULL inode is d_real_inode() from trace_uprobe and this caller is
	 * likely going to be followed reading from the file, before placing
	 * uprobes on offset within the file, so lowerdata should be available
	 * when setting the uprobe.
	 */
	ovl_maybe_lookup_lowerdata(dentry);
	lower = ovl_dentry_lowerdata(dentry);
	if (!lower)
		goto bug;
	real = lower;

	/* Handle recursion */
	real = d_real(real, inode);

	if (!inode || inode == d_inode(real))
		return real;
bug:
	WARN(1, "%s(%pd4, %s:%lu): real dentry (%p/%lu) not found\n",
	     __func__, dentry, inode ? inode->i_sb->s_id : "NULL",
	     inode ? inode->i_ino : 0, real,
	     real && d_inode(real) ? d_inode(real)->i_ino : 0);
	return dentry;
}

static int ovl_revalidate_real(struct dentry *d, unsigned int flags, bool weak)
{
	int ret = 1;

	if (!d)
		return 1;

	if (weak) {
		if (d->d_flags & DCACHE_OP_WEAK_REVALIDATE)
			ret =  d->d_op->d_weak_revalidate(d, flags);
	} else if (d->d_flags & DCACHE_OP_REVALIDATE) {
		ret = d->d_op->d_revalidate(d, flags);
		if (!ret) {
			if (!(flags & LOOKUP_RCU))
				d_invalidate(d);
			ret = -ESTALE;
		}
	}
	return ret;
}

static int ovl_dentry_revalidate_common(struct dentry *dentry,
					unsigned int flags, bool weak)
{
	struct ovl_entry *oe = OVL_E(dentry);
	struct ovl_path *lowerstack = ovl_lowerstack(oe);
	struct inode *inode = d_inode_rcu(dentry);
	struct dentry *upper;
	unsigned int i;
	int ret = 1;

	/* Careful in RCU mode */
	if (!inode)
		return -ECHILD;

	upper = ovl_i_dentry_upper(inode);
	if (upper)
		ret = ovl_revalidate_real(upper, flags, weak);

	for (i = 0; ret > 0 && i < ovl_numlower(oe); i++)
		ret = ovl_revalidate_real(lowerstack[i].dentry, flags, weak);

	return ret;
}

static int ovl_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	return ovl_dentry_revalidate_common(dentry, flags, false);
}

static int ovl_dentry_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	return ovl_dentry_revalidate_common(dentry, flags, true);
}

static const struct dentry_operations ovl_dentry_operations = {
	.d_real = ovl_d_real,
	.d_revalidate = ovl_dentry_revalidate,
	.d_weak_revalidate = ovl_dentry_weak_revalidate,
};

static struct kmem_cache *ovl_inode_cachep;

static struct inode *ovl_alloc_inode(struct super_block *sb)
{
	struct ovl_inode *oi = alloc_inode_sb(sb, ovl_inode_cachep, GFP_KERNEL);

	if (!oi)
		return NULL;

	oi->cache = NULL;
	oi->redirect = NULL;
	oi->version = 0;
	oi->flags = 0;
	oi->__upperdentry = NULL;
	oi->lowerdata_redirect = NULL;
	oi->oe = NULL;
	mutex_init(&oi->lock);

	return &oi->vfs_inode;
}

static void ovl_free_inode(struct inode *inode)
{
	struct ovl_inode *oi = OVL_I(inode);

	kfree(oi->redirect);
	mutex_destroy(&oi->lock);
	kmem_cache_free(ovl_inode_cachep, oi);
}

static void ovl_destroy_inode(struct inode *inode)
{
	struct ovl_inode *oi = OVL_I(inode);

	dput(oi->__upperdentry);
	ovl_free_entry(oi->oe);
	if (S_ISDIR(inode->i_mode))
		ovl_dir_cache_free(inode);
	else
		kfree(oi->lowerdata_redirect);
}

static void ovl_free_fs(struct ovl_fs *ofs)
{
	struct vfsmount **mounts;
	unsigned i;

	iput(ofs->workbasedir_trap);
	iput(ofs->indexdir_trap);
	iput(ofs->workdir_trap);
	dput(ofs->whiteout);
	dput(ofs->indexdir);
	dput(ofs->workdir);
	if (ofs->workdir_locked)
		ovl_inuse_unlock(ofs->workbasedir);
	dput(ofs->workbasedir);
	if (ofs->upperdir_locked)
		ovl_inuse_unlock(ovl_upper_mnt(ofs)->mnt_root);

	/* Hack!  Reuse ofs->layers as a vfsmount array before freeing it */
	mounts = (struct vfsmount **) ofs->layers;
	for (i = 0; i < ofs->numlayer; i++) {
		iput(ofs->layers[i].trap);
		mounts[i] = ofs->layers[i].mnt;
		kfree(ofs->layers[i].name);
	}
	kern_unmount_array(mounts, ofs->numlayer);
	kfree(ofs->layers);
	for (i = 0; i < ofs->numfs; i++)
		free_anon_bdev(ofs->fs[i].pseudo_dev);
	kfree(ofs->fs);

	kfree(ofs->config.upperdir);
	kfree(ofs->config.workdir);
	if (ofs->creator_cred)
		put_cred(ofs->creator_cred);
	kfree(ofs);
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	if (ofs)
		ovl_free_fs(ofs);
}

/* Sync real dirty inodes in upper filesystem (if it exists) */
static int ovl_sync_fs(struct super_block *sb, int wait)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct super_block *upper_sb;
	int ret;

	ret = ovl_sync_status(ofs);
	/*
	 * We have to always set the err, because the return value isn't
	 * checked in syncfs, and instead indirectly return an error via
	 * the sb's writeback errseq, which VFS inspects after this call.
	 */
	if (ret < 0) {
		errseq_set(&sb->s_wb_err, -EIO);
		return -EIO;
	}

	if (!ret)
		return ret;

	/*
	 * Not called for sync(2) call or an emergency sync (SB_I_SKIP_SYNC).
	 * All the super blocks will be iterated, including upper_sb.
	 *
	 * If this is a syncfs(2) call, then we do need to call
	 * sync_filesystem() on upper_sb, but enough if we do it when being
	 * called with wait == 1.
	 */
	if (!wait)
		return 0;

	upper_sb = ovl_upper_mnt(ofs)->mnt_sb;

	down_read(&upper_sb->s_umount);
	ret = sync_filesystem(upper_sb);
	up_read(&upper_sb->s_umount);

	return ret;
}

/**
 * ovl_statfs
 * @dentry: The dentry to query
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics.  As writes always target the upper layer
 * filesystem pass the statfs to the upper filesystem (if it exists)
 */
static int ovl_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct dentry *root_dentry = dentry->d_sb->s_root;
	struct path path;
	int err;

	ovl_path_real(root_dentry, &path);

	err = vfs_statfs(&path, buf);
	if (!err) {
		buf->f_namelen = ofs->namelen;
		buf->f_type = OVERLAYFS_SUPER_MAGIC;
	}

	return err;
}

/* Will this overlay be forced to mount/remount ro? */
static bool ovl_force_readonly(struct ovl_fs *ofs)
{
	return (!ovl_upper_mnt(ofs) || !ofs->workdir);
}

static const struct constant_table ovl_parameter_redirect_dir[] = {
	{ "off",	OVL_REDIRECT_OFF      },
	{ "follow",	OVL_REDIRECT_FOLLOW   },
	{ "nofollow",	OVL_REDIRECT_NOFOLLOW },
	{ "on",		OVL_REDIRECT_ON       },
	{}
};

static const char *ovl_redirect_mode(struct ovl_config *config)
{
	return ovl_parameter_redirect_dir[config->redirect_mode].name;
}

static int ovl_redirect_mode_def(void)
{
	return ovl_redirect_dir_def ? OVL_REDIRECT_ON :
		ovl_redirect_always_follow ? OVL_REDIRECT_FOLLOW :
					     OVL_REDIRECT_NOFOLLOW;
}

static const struct constant_table ovl_parameter_xino[] = {
	{ "off",	OVL_XINO_OFF  },
	{ "auto",	OVL_XINO_AUTO },
	{ "on",		OVL_XINO_ON   },
	{}
};

static const char *ovl_xino_mode(struct ovl_config *config)
{
	return ovl_parameter_xino[config->xino].name;
}

static inline int ovl_xino_def(void)
{
	return ovl_xino_auto_def ? OVL_XINO_AUTO : OVL_XINO_OFF;
}

/**
 * ovl_show_options
 * @m: the seq_file handle
 * @dentry: The dentry to query
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does not fail.
 */
static int ovl_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct ovl_fs *ofs = sb->s_fs_info;
	size_t nr, nr_merged_lower = ofs->numlayer - ofs->numdatalayer;
	const struct ovl_layer *data_layers = &ofs->layers[nr_merged_lower];

	/* ofs->layers[0] is the upper layer */
	seq_printf(m, ",lowerdir=%s", ofs->layers[1].name);
	/* dump regular lower layers */
	for (nr = 2; nr < nr_merged_lower; nr++)
		seq_printf(m, ":%s", ofs->layers[nr].name);
	/* dump data lower layers */
	for (nr = 0; nr < ofs->numdatalayer; nr++)
		seq_printf(m, "::%s", data_layers[nr].name);
	if (ofs->config.upperdir) {
		seq_show_option(m, "upperdir", ofs->config.upperdir);
		seq_show_option(m, "workdir", ofs->config.workdir);
	}
	if (ofs->config.default_permissions)
		seq_puts(m, ",default_permissions");
	if (ofs->config.redirect_mode != ovl_redirect_mode_def())
		seq_printf(m, ",redirect_dir=%s",
			   ovl_redirect_mode(&ofs->config));
	if (ofs->config.index != ovl_index_def)
		seq_printf(m, ",index=%s", ofs->config.index ? "on" : "off");
	if (!ofs->config.uuid)
		seq_puts(m, ",uuid=off");
	if (ofs->config.nfs_export != ovl_nfs_export_def)
		seq_printf(m, ",nfs_export=%s", ofs->config.nfs_export ?
						"on" : "off");
	if (ofs->config.xino != ovl_xino_def() && !ovl_same_fs(ofs))
		seq_printf(m, ",xino=%s", ovl_xino_mode(&ofs->config));
	if (ofs->config.metacopy != ovl_metacopy_def)
		seq_printf(m, ",metacopy=%s",
			   ofs->config.metacopy ? "on" : "off");
	if (ofs->config.ovl_volatile)
		seq_puts(m, ",volatile");
	if (ofs->config.userxattr)
		seq_puts(m, ",userxattr");
	return 0;
}

static int ovl_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct ovl_fs *ofs = sb->s_fs_info;
	struct super_block *upper_sb;
	int ret = 0;

	if (!(fc->sb_flags & SB_RDONLY) && ovl_force_readonly(ofs))
		return -EROFS;

	if (fc->sb_flags & SB_RDONLY && !sb_rdonly(sb)) {
		upper_sb = ovl_upper_mnt(ofs)->mnt_sb;
		if (ovl_should_sync(ofs)) {
			down_read(&upper_sb->s_umount);
			ret = sync_filesystem(upper_sb);
			up_read(&upper_sb->s_umount);
		}
	}

	return ret;
}

static const struct super_operations ovl_super_operations = {
	.alloc_inode	= ovl_alloc_inode,
	.free_inode	= ovl_free_inode,
	.destroy_inode	= ovl_destroy_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= ovl_put_super,
	.sync_fs	= ovl_sync_fs,
	.statfs		= ovl_statfs,
	.show_options	= ovl_show_options,
};

enum {
	Opt_lowerdir,
	Opt_upperdir,
	Opt_workdir,
	Opt_default_permissions,
	Opt_redirect_dir,
	Opt_index,
	Opt_uuid,
	Opt_nfs_export,
	Opt_userxattr,
	Opt_xino,
	Opt_metacopy,
	Opt_volatile,
};

static const struct constant_table ovl_parameter_bool[] = {
	{ "on",		true  },
	{ "off",	false },
	{}
};

#define fsparam_string_empty(NAME, OPT) \
	__fsparam(fs_param_is_string, NAME, OPT, fs_param_can_be_empty, NULL)

static const struct fs_parameter_spec ovl_parameter_spec[] = {
	fsparam_string_empty("lowerdir",    Opt_lowerdir),
	fsparam_string("upperdir",          Opt_upperdir),
	fsparam_string("workdir",           Opt_workdir),
	fsparam_flag("default_permissions", Opt_default_permissions),
	fsparam_enum("redirect_dir",        Opt_redirect_dir, ovl_parameter_redirect_dir),
	fsparam_enum("index",               Opt_index, ovl_parameter_bool),
	fsparam_enum("uuid",                Opt_uuid, ovl_parameter_bool),
	fsparam_enum("nfs_export",          Opt_nfs_export, ovl_parameter_bool),
	fsparam_flag("userxattr",           Opt_userxattr),
	fsparam_enum("xino",                Opt_xino, ovl_parameter_xino),
	fsparam_enum("metacopy",            Opt_metacopy, ovl_parameter_bool),
	fsparam_flag("volatile",            Opt_volatile),
	{}
};

static int ovl_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	int err = 0;
	struct fs_parse_result result;
	struct ovl_fs *ofs = fc->s_fs_info;
	struct ovl_config *config = &ofs->config;
	struct ovl_fs_context *ctx = fc->fs_private;
	int opt;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		/*
		 * On remount overlayfs has always ignored all mount
		 * options no matter if malformed or not so for
		 * backwards compatibility we do the same here.
		 */
		if (fc->oldapi)
			return 0;

		/*
		 * Give us the freedom to allow changing mount options
		 * with the new mount api in the future. So instead of
		 * silently ignoring everything we report a proper
		 * error. This is only visible for users of the new
		 * mount api.
		 */
		return invalfc(fc, "No changes allowed in reconfigure");
	}

	opt = fs_parse(fc, ovl_parameter_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_lowerdir:
		err = ovl_parse_param_lowerdir(param->string, fc);
		break;
	case Opt_upperdir:
		fallthrough;
	case Opt_workdir:
		err = ovl_parse_param_upperdir(param->string, fc,
					       (Opt_workdir == opt));
		break;
	case Opt_default_permissions:
		config->default_permissions = true;
		break;
	case Opt_redirect_dir:
		config->redirect_mode = result.uint_32;
		if (config->redirect_mode == OVL_REDIRECT_OFF) {
			config->redirect_mode = ovl_redirect_always_follow ?
						OVL_REDIRECT_FOLLOW :
						OVL_REDIRECT_NOFOLLOW;
		}
		ctx->set.redirect = true;
		break;
	case Opt_index:
		config->index = result.uint_32;
		ctx->set.index = true;
		break;
	case Opt_uuid:
		config->uuid = result.uint_32;
		break;
	case Opt_nfs_export:
		config->nfs_export = result.uint_32;
		ctx->set.nfs_export = true;
		break;
	case Opt_xino:
		config->xino = result.uint_32;
		break;
	case Opt_metacopy:
		config->metacopy = result.uint_32;
		ctx->set.metacopy = true;
		break;
	case Opt_volatile:
		config->ovl_volatile = true;
		break;
	case Opt_userxattr:
		config->userxattr = true;
		break;
	default:
		pr_err("unrecognized mount option \"%s\" or missing value\n",
		       param->key);
		return -EINVAL;
	}

	return err;
}

static int ovl_fs_params_verify(const struct ovl_fs_context *ctx,
				struct ovl_config *config)
{
	struct ovl_opt_set set = ctx->set;

	if (ctx->nr_data > 0 && !config->metacopy) {
		pr_err("lower data-only dirs require metacopy support.\n");
		return -EINVAL;
	}

	/* Workdir/index are useless in non-upper mount */
	if (!config->upperdir) {
		if (config->workdir) {
			pr_info("option \"workdir=%s\" is useless in a non-upper mount, ignore\n",
				config->workdir);
			kfree(config->workdir);
			config->workdir = NULL;
		}
		if (config->index && set.index) {
			pr_info("option \"index=on\" is useless in a non-upper mount, ignore\n");
			set.index = false;
		}
		config->index = false;
	}

	if (!config->upperdir && config->ovl_volatile) {
		pr_info("option \"volatile\" is meaningless in a non-upper mount, ignoring it.\n");
		config->ovl_volatile = false;
	}

	/*
	 * This is to make the logic below simpler.  It doesn't make any other
	 * difference, since redirect_dir=on is only used for upper.
	 */
	if (!config->upperdir && config->redirect_mode == OVL_REDIRECT_FOLLOW)
		config->redirect_mode = OVL_REDIRECT_ON;

	/* Resolve metacopy -> redirect_dir dependency */
	if (config->metacopy && config->redirect_mode != OVL_REDIRECT_ON) {
		if (set.metacopy && set.redirect) {
			pr_err("conflicting options: metacopy=on,redirect_dir=%s\n",
			       ovl_redirect_mode(config));
			return -EINVAL;
		}
		if (set.redirect) {
			/*
			 * There was an explicit redirect_dir=... that resulted
			 * in this conflict.
			 */
			pr_info("disabling metacopy due to redirect_dir=%s\n",
				ovl_redirect_mode(config));
			config->metacopy = false;
		} else {
			/* Automatically enable redirect otherwise. */
			config->redirect_mode = OVL_REDIRECT_ON;
		}
	}

	/* Resolve nfs_export -> index dependency */
	if (config->nfs_export && !config->index) {
		if (!config->upperdir &&
		    config->redirect_mode != OVL_REDIRECT_NOFOLLOW) {
			pr_info("NFS export requires \"redirect_dir=nofollow\" on non-upper mount, falling back to nfs_export=off.\n");
			config->nfs_export = false;
		} else if (set.nfs_export && set.index) {
			pr_err("conflicting options: nfs_export=on,index=off\n");
			return -EINVAL;
		} else if (set.index) {
			/*
			 * There was an explicit index=off that resulted
			 * in this conflict.
			 */
			pr_info("disabling nfs_export due to index=off\n");
			config->nfs_export = false;
		} else {
			/* Automatically enable index otherwise. */
			config->index = true;
		}
	}

	/* Resolve nfs_export -> !metacopy dependency */
	if (config->nfs_export && config->metacopy) {
		if (set.nfs_export && set.metacopy) {
			pr_err("conflicting options: nfs_export=on,metacopy=on\n");
			return -EINVAL;
		}
		if (set.metacopy) {
			/*
			 * There was an explicit metacopy=on that resulted
			 * in this conflict.
			 */
			pr_info("disabling nfs_export due to metacopy=on\n");
			config->nfs_export = false;
		} else {
			/*
			 * There was an explicit nfs_export=on that resulted
			 * in this conflict.
			 */
			pr_info("disabling metacopy due to nfs_export=on\n");
			config->metacopy = false;
		}
	}


	/* Resolve userxattr -> !redirect && !metacopy dependency */
	if (config->userxattr) {
		if (set.redirect &&
		    config->redirect_mode != OVL_REDIRECT_NOFOLLOW) {
			pr_err("conflicting options: userxattr,redirect_dir=%s\n",
			       ovl_redirect_mode(config));
			return -EINVAL;
		}
		if (config->metacopy && set.metacopy) {
			pr_err("conflicting options: userxattr,metacopy=on\n");
			return -EINVAL;
		}
		/*
		 * Silently disable default setting of redirect and metacopy.
		 * This shall be the default in the future as well: these
		 * options must be explicitly enabled if used together with
		 * userxattr.
		 */
		config->redirect_mode = OVL_REDIRECT_NOFOLLOW;
		config->metacopy = false;
	}

	return 0;
}

#define OVL_WORKDIR_NAME "work"
#define OVL_INDEXDIR_NAME "index"

static struct dentry *ovl_workdir_create(struct ovl_fs *ofs,
					 const char *name, bool persist)
{
	struct inode *dir =  ofs->workbasedir->d_inode;
	struct vfsmount *mnt = ovl_upper_mnt(ofs);
	struct dentry *work;
	int err;
	bool retried = false;

	inode_lock_nested(dir, I_MUTEX_PARENT);
retry:
	work = ovl_lookup_upper(ofs, name, ofs->workbasedir, strlen(name));

	if (!IS_ERR(work)) {
		struct iattr attr = {
			.ia_valid = ATTR_MODE,
			.ia_mode = S_IFDIR | 0,
		};

		if (work->d_inode) {
			err = -EEXIST;
			if (retried)
				goto out_dput;

			if (persist)
				goto out_unlock;

			retried = true;
			err = ovl_workdir_cleanup(ofs, dir, mnt, work, 0);
			dput(work);
			if (err == -EINVAL) {
				work = ERR_PTR(err);
				goto out_unlock;
			}
			goto retry;
		}

		err = ovl_mkdir_real(ofs, dir, &work, attr.ia_mode);
		if (err)
			goto out_dput;

		/* Weird filesystem returning with hashed negative (kernfs)? */
		err = -EINVAL;
		if (d_really_is_negative(work))
			goto out_dput;

		/*
		 * Try to remove POSIX ACL xattrs from workdir.  We are good if:
		 *
		 * a) success (there was a POSIX ACL xattr and was removed)
		 * b) -ENODATA (there was no POSIX ACL xattr)
		 * c) -EOPNOTSUPP (POSIX ACL xattrs are not supported)
		 *
		 * There are various other error values that could effectively
		 * mean that the xattr doesn't exist (e.g. -ERANGE is returned
		 * if the xattr name is too long), but the set of filesystems
		 * allowed as upper are limited to "normal" ones, where checking
		 * for the above two errors is sufficient.
		 */
		err = ovl_do_remove_acl(ofs, work, XATTR_NAME_POSIX_ACL_DEFAULT);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		err = ovl_do_remove_acl(ofs, work, XATTR_NAME_POSIX_ACL_ACCESS);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		/* Clear any inherited mode bits */
		inode_lock(work->d_inode);
		err = ovl_do_notify_change(ofs, work, &attr);
		inode_unlock(work->d_inode);
		if (err)
			goto out_dput;
	} else {
		err = PTR_ERR(work);
		goto out_err;
	}
out_unlock:
	inode_unlock(dir);
	return work;

out_dput:
	dput(work);
out_err:
	pr_warn("failed to create directory %s/%s (errno: %i); mounting read-only\n",
		ofs->config.workdir, name, -err);
	work = NULL;
	goto out_unlock;
}

static int ovl_check_namelen(const struct path *path, struct ovl_fs *ofs,
			     const char *name)
{
	struct kstatfs statfs;
	int err = vfs_statfs(path, &statfs);

	if (err)
		pr_err("statfs failed on '%s'\n", name);
	else
		ofs->namelen = max(ofs->namelen, statfs.f_namelen);

	return err;
}

static int ovl_lower_dir(const char *name, struct path *path,
			 struct ovl_fs *ofs, int *stack_depth)
{
	int fh_type;
	int err;

	err = ovl_check_namelen(path, ofs, name);
	if (err)
		return err;

	*stack_depth = max(*stack_depth, path->mnt->mnt_sb->s_stack_depth);

	/*
	 * The inodes index feature and NFS export need to encode and decode
	 * file handles, so they require that all layers support them.
	 */
	fh_type = ovl_can_decode_fh(path->dentry->d_sb);
	if ((ofs->config.nfs_export ||
	     (ofs->config.index && ofs->config.upperdir)) && !fh_type) {
		ofs->config.index = false;
		ofs->config.nfs_export = false;
		pr_warn("fs on '%s' does not support file handles, falling back to index=off,nfs_export=off.\n",
			name);
	}
	/*
	 * Decoding origin file handle is required for persistent st_ino.
	 * Without persistent st_ino, xino=auto falls back to xino=off.
	 */
	if (ofs->config.xino == OVL_XINO_AUTO &&
	    ofs->config.upperdir && !fh_type) {
		ofs->config.xino = OVL_XINO_OFF;
		pr_warn("fs on '%s' does not support file handles, falling back to xino=off.\n",
			name);
	}

	/* Check if lower fs has 32bit inode numbers */
	if (fh_type != FILEID_INO32_GEN)
		ofs->xino_mode = -1;

	return 0;
}

/* Workdir should not be subdir of upperdir and vice versa */
static bool ovl_workdir_ok(struct dentry *workdir, struct dentry *upperdir)
{
	bool ok = false;

	if (workdir != upperdir) {
		ok = (lock_rename(workdir, upperdir) == NULL);
		unlock_rename(workdir, upperdir);
	}
	return ok;
}

static int ovl_own_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static int ovl_own_xattr_set(const struct xattr_handler *handler,
			     struct mnt_idmap *idmap,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, const void *value,
			     size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static int ovl_other_xattr_get(const struct xattr_handler *handler,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, inode, name, buffer, size);
}

static int ovl_other_xattr_set(const struct xattr_handler *handler,
			       struct mnt_idmap *idmap,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, const void *value,
			       size_t size, int flags)
{
	return ovl_xattr_set(dentry, inode, name, value, size, flags);
}

static const struct xattr_handler ovl_own_trusted_xattr_handler = {
	.prefix	= OVL_XATTR_TRUSTED_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_own_user_xattr_handler = {
	.prefix	= OVL_XATTR_USER_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_other_xattr_handler = {
	.prefix	= "", /* catch all */
	.get = ovl_other_xattr_get,
	.set = ovl_other_xattr_set,
};

static const struct xattr_handler *ovl_trusted_xattr_handlers[] = {
	&ovl_own_trusted_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static const struct xattr_handler *ovl_user_xattr_handlers[] = {
	&ovl_own_user_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static int ovl_setup_trap(struct super_block *sb, struct dentry *dir,
			  struct inode **ptrap, const char *name)
{
	struct inode *trap;
	int err;

	trap = ovl_get_trap_inode(sb, dir);
	err = PTR_ERR_OR_ZERO(trap);
	if (err) {
		if (err == -ELOOP)
			pr_err("conflicting %s path\n", name);
		return err;
	}

	*ptrap = trap;
	return 0;
}

/*
 * Determine how we treat concurrent use of upperdir/workdir based on the
 * index feature. This is papering over mount leaks of container runtimes,
 * for example, an old overlay mount is leaked and now its upperdir is
 * attempted to be used as a lower layer in a new overlay mount.
 */
static int ovl_report_in_use(struct ovl_fs *ofs, const char *name)
{
	if (ofs->config.index) {
		pr_err("%s is in-use as upperdir/workdir of another mount, mount with '-o index=off' to override exclusive upperdir protection.\n",
		       name);
		return -EBUSY;
	} else {
		pr_warn("%s is in-use as upperdir/workdir of another mount, accessing files from both mounts will result in undefined behavior.\n",
			name);
		return 0;
	}
}

static int ovl_get_upper(struct super_block *sb, struct ovl_fs *ofs,
			 struct ovl_layer *upper_layer,
			 const struct path *upperpath)
{
	struct vfsmount *upper_mnt;
	int err;

	/* Upperdir path should not be r/o */
	if (__mnt_is_readonly(upperpath->mnt)) {
		pr_err("upper fs is r/o, try multi-lower layers mount\n");
		err = -EINVAL;
		goto out;
	}

	err = ovl_check_namelen(upperpath, ofs, ofs->config.upperdir);
	if (err)
		goto out;

	err = ovl_setup_trap(sb, upperpath->dentry, &upper_layer->trap,
			     "upperdir");
	if (err)
		goto out;

	upper_mnt = clone_private_mount(upperpath);
	err = PTR_ERR(upper_mnt);
	if (IS_ERR(upper_mnt)) {
		pr_err("failed to clone upperpath\n");
		goto out;
	}

	/* Don't inherit atime flags */
	upper_mnt->mnt_flags &= ~(MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME);
	upper_layer->mnt = upper_mnt;
	upper_layer->idx = 0;
	upper_layer->fsid = 0;

	err = -ENOMEM;
	upper_layer->name = kstrdup(ofs->config.upperdir, GFP_KERNEL);
	if (!upper_layer->name)
		goto out;

	/*
	 * Inherit SB_NOSEC flag from upperdir.
	 *
	 * This optimization changes behavior when a security related attribute
	 * (suid/sgid/security.*) is changed on an underlying layer.  This is
	 * okay because we don't yet have guarantees in that case, but it will
	 * need careful treatment once we want to honour changes to underlying
	 * filesystems.
	 */
	if (upper_mnt->mnt_sb->s_flags & SB_NOSEC)
		sb->s_flags |= SB_NOSEC;

	if (ovl_inuse_trylock(ovl_upper_mnt(ofs)->mnt_root)) {
		ofs->upperdir_locked = true;
	} else {
		err = ovl_report_in_use(ofs, "upperdir");
		if (err)
			goto out;
	}

	err = 0;
out:
	return err;
}

/*
 * Returns 1 if RENAME_WHITEOUT is supported, 0 if not supported and
 * negative values if error is encountered.
 */
static int ovl_check_rename_whiteout(struct ovl_fs *ofs)
{
	struct dentry *workdir = ofs->workdir;
	struct inode *dir = d_inode(workdir);
	struct dentry *temp;
	struct dentry *dest;
	struct dentry *whiteout;
	struct name_snapshot name;
	int err;

	inode_lock_nested(dir, I_MUTEX_PARENT);

	temp = ovl_create_temp(ofs, workdir, OVL_CATTR(S_IFREG | 0));
	err = PTR_ERR(temp);
	if (IS_ERR(temp))
		goto out_unlock;

	dest = ovl_lookup_temp(ofs, workdir);
	err = PTR_ERR(dest);
	if (IS_ERR(dest)) {
		dput(temp);
		goto out_unlock;
	}

	/* Name is inline and stable - using snapshot as a copy helper */
	take_dentry_name_snapshot(&name, temp);
	err = ovl_do_rename(ofs, dir, temp, dir, dest, RENAME_WHITEOUT);
	if (err) {
		if (err == -EINVAL)
			err = 0;
		goto cleanup_temp;
	}

	whiteout = ovl_lookup_upper(ofs, name.name.name, workdir, name.name.len);
	err = PTR_ERR(whiteout);
	if (IS_ERR(whiteout))
		goto cleanup_temp;

	err = ovl_is_whiteout(whiteout);

	/* Best effort cleanup of whiteout and temp file */
	if (err)
		ovl_cleanup(ofs, dir, whiteout);
	dput(whiteout);

cleanup_temp:
	ovl_cleanup(ofs, dir, temp);
	release_dentry_name_snapshot(&name);
	dput(temp);
	dput(dest);

out_unlock:
	inode_unlock(dir);

	return err;
}

static struct dentry *ovl_lookup_or_create(struct ovl_fs *ofs,
					   struct dentry *parent,
					   const char *name, umode_t mode)
{
	size_t len = strlen(name);
	struct dentry *child;

	inode_lock_nested(parent->d_inode, I_MUTEX_PARENT);
	child = ovl_lookup_upper(ofs, name, parent, len);
	if (!IS_ERR(child) && !child->d_inode)
		child = ovl_create_real(ofs, parent->d_inode, child,
					OVL_CATTR(mode));
	inode_unlock(parent->d_inode);
	dput(parent);

	return child;
}

/*
 * Creates $workdir/work/incompat/volatile/dirty file if it is not already
 * present.
 */
static int ovl_create_volatile_dirty(struct ovl_fs *ofs)
{
	unsigned int ctr;
	struct dentry *d = dget(ofs->workbasedir);
	static const char *const volatile_path[] = {
		OVL_WORKDIR_NAME, "incompat", "volatile", "dirty"
	};
	const char *const *name = volatile_path;

	for (ctr = ARRAY_SIZE(volatile_path); ctr; ctr--, name++) {
		d = ovl_lookup_or_create(ofs, d, *name, ctr > 1 ? S_IFDIR : S_IFREG);
		if (IS_ERR(d))
			return PTR_ERR(d);
	}
	dput(d);
	return 0;
}

static int ovl_make_workdir(struct super_block *sb, struct ovl_fs *ofs,
			    const struct path *workpath)
{
	struct vfsmount *mnt = ovl_upper_mnt(ofs);
	struct dentry *workdir;
	struct file *tmpfile;
	bool rename_whiteout;
	bool d_type;
	int fh_type;
	int err;

	err = mnt_want_write(mnt);
	if (err)
		return err;

	workdir = ovl_workdir_create(ofs, OVL_WORKDIR_NAME, false);
	err = PTR_ERR(workdir);
	if (IS_ERR_OR_NULL(workdir))
		goto out;

	ofs->workdir = workdir;

	err = ovl_setup_trap(sb, ofs->workdir, &ofs->workdir_trap, "workdir");
	if (err)
		goto out;

	/*
	 * Upper should support d_type, else whiteouts are visible.  Given
	 * workdir and upper are on same fs, we can do iterate_dir() on
	 * workdir. This check requires successful creation of workdir in
	 * previous step.
	 */
	err = ovl_check_d_type_supported(workpath);
	if (err < 0)
		goto out;

	d_type = err;
	if (!d_type)
		pr_warn("upper fs needs to support d_type.\n");

	/* Check if upper/work fs supports O_TMPFILE */
	tmpfile = ovl_do_tmpfile(ofs, ofs->workdir, S_IFREG | 0);
	ofs->tmpfile = !IS_ERR(tmpfile);
	if (ofs->tmpfile)
		fput(tmpfile);
	else
		pr_warn("upper fs does not support tmpfile.\n");


	/* Check if upper/work fs supports RENAME_WHITEOUT */
	err = ovl_check_rename_whiteout(ofs);
	if (err < 0)
		goto out;

	rename_whiteout = err;
	if (!rename_whiteout)
		pr_warn("upper fs does not support RENAME_WHITEOUT.\n");

	/*
	 * Check if upper/work fs supports (trusted|user).overlay.* xattr
	 */
	err = ovl_setxattr(ofs, ofs->workdir, OVL_XATTR_OPAQUE, "0", 1);
	if (err) {
		pr_warn("failed to set xattr on upper\n");
		ofs->noxattr = true;
		if (ovl_redirect_follow(ofs)) {
			ofs->config.redirect_mode = OVL_REDIRECT_NOFOLLOW;
			pr_warn("...falling back to redirect_dir=nofollow.\n");
		}
		if (ofs->config.metacopy) {
			ofs->config.metacopy = false;
			pr_warn("...falling back to metacopy=off.\n");
		}
		if (ofs->config.index) {
			ofs->config.index = false;
			pr_warn("...falling back to index=off.\n");
		}
		/*
		 * xattr support is required for persistent st_ino.
		 * Without persistent st_ino, xino=auto falls back to xino=off.
		 */
		if (ofs->config.xino == OVL_XINO_AUTO) {
			ofs->config.xino = OVL_XINO_OFF;
			pr_warn("...falling back to xino=off.\n");
		}
		if (err == -EPERM && !ofs->config.userxattr)
			pr_info("try mounting with 'userxattr' option\n");
		err = 0;
	} else {
		ovl_removexattr(ofs, ofs->workdir, OVL_XATTR_OPAQUE);
	}

	/*
	 * We allowed sub-optimal upper fs configuration and don't want to break
	 * users over kernel upgrade, but we never allowed remote upper fs, so
	 * we can enforce strict requirements for remote upper fs.
	 */
	if (ovl_dentry_remote(ofs->workdir) &&
	    (!d_type || !rename_whiteout || ofs->noxattr)) {
		pr_err("upper fs missing required features.\n");
		err = -EINVAL;
		goto out;
	}

	/*
	 * For volatile mount, create a incompat/volatile/dirty file to keep
	 * track of it.
	 */
	if (ofs->config.ovl_volatile) {
		err = ovl_create_volatile_dirty(ofs);
		if (err < 0) {
			pr_err("Failed to create volatile/dirty file.\n");
			goto out;
		}
	}

	/* Check if upper/work fs supports file handles */
	fh_type = ovl_can_decode_fh(ofs->workdir->d_sb);
	if (ofs->config.index && !fh_type) {
		ofs->config.index = false;
		pr_warn("upper fs does not support file handles, falling back to index=off.\n");
	}

	/* Check if upper fs has 32bit inode numbers */
	if (fh_type != FILEID_INO32_GEN)
		ofs->xino_mode = -1;

	/* NFS export of r/w mount depends on index */
	if (ofs->config.nfs_export && !ofs->config.index) {
		pr_warn("NFS export requires \"index=on\", falling back to nfs_export=off.\n");
		ofs->config.nfs_export = false;
	}
out:
	mnt_drop_write(mnt);
	return err;
}

static int ovl_get_workdir(struct super_block *sb, struct ovl_fs *ofs,
			   const struct path *upperpath,
			   const struct path *workpath)
{
	int err;

	err = -EINVAL;
	if (upperpath->mnt != workpath->mnt) {
		pr_err("workdir and upperdir must reside under the same mount\n");
		return err;
	}
	if (!ovl_workdir_ok(workpath->dentry, upperpath->dentry)) {
		pr_err("workdir and upperdir must be separate subtrees\n");
		return err;
	}

	ofs->workbasedir = dget(workpath->dentry);

	if (ovl_inuse_trylock(ofs->workbasedir)) {
		ofs->workdir_locked = true;
	} else {
		err = ovl_report_in_use(ofs, "workdir");
		if (err)
			return err;
	}

	err = ovl_setup_trap(sb, ofs->workbasedir, &ofs->workbasedir_trap,
			     "workdir");
	if (err)
		return err;

	return ovl_make_workdir(sb, ofs, workpath);
}

static int ovl_get_indexdir(struct super_block *sb, struct ovl_fs *ofs,
			    struct ovl_entry *oe, const struct path *upperpath)
{
	struct vfsmount *mnt = ovl_upper_mnt(ofs);
	struct dentry *indexdir;
	int err;

	err = mnt_want_write(mnt);
	if (err)
		return err;

	/* Verify lower root is upper root origin */
	err = ovl_verify_origin(ofs, upperpath->dentry,
				ovl_lowerstack(oe)->dentry, true);
	if (err) {
		pr_err("failed to verify upper root origin\n");
		goto out;
	}

	/* index dir will act also as workdir */
	iput(ofs->workdir_trap);
	ofs->workdir_trap = NULL;
	dput(ofs->workdir);
	ofs->workdir = NULL;
	indexdir = ovl_workdir_create(ofs, OVL_INDEXDIR_NAME, true);
	if (IS_ERR(indexdir)) {
		err = PTR_ERR(indexdir);
	} else if (indexdir) {
		ofs->indexdir = indexdir;
		ofs->workdir = dget(indexdir);

		err = ovl_setup_trap(sb, ofs->indexdir, &ofs->indexdir_trap,
				     "indexdir");
		if (err)
			goto out;

		/*
		 * Verify upper root is exclusively associated with index dir.
		 * Older kernels stored upper fh in ".overlay.origin"
		 * xattr. If that xattr exists, verify that it is a match to
		 * upper dir file handle. In any case, verify or set xattr
		 * ".overlay.upper" to indicate that index may have
		 * directory entries.
		 */
		if (ovl_check_origin_xattr(ofs, ofs->indexdir)) {
			err = ovl_verify_set_fh(ofs, ofs->indexdir,
						OVL_XATTR_ORIGIN,
						upperpath->dentry, true, false);
			if (err)
				pr_err("failed to verify index dir 'origin' xattr\n");
		}
		err = ovl_verify_upper(ofs, ofs->indexdir, upperpath->dentry,
				       true);
		if (err)
			pr_err("failed to verify index dir 'upper' xattr\n");

		/* Cleanup bad/stale/orphan index entries */
		if (!err)
			err = ovl_indexdir_cleanup(ofs);
	}
	if (err || !ofs->indexdir)
		pr_warn("try deleting index dir or mounting with '-o index=off' to disable inodes index.\n");

out:
	mnt_drop_write(mnt);
	return err;
}

static bool ovl_lower_uuid_ok(struct ovl_fs *ofs, const uuid_t *uuid)
{
	unsigned int i;

	if (!ofs->config.nfs_export && !ovl_upper_mnt(ofs))
		return true;

	/*
	 * We allow using single lower with null uuid for index and nfs_export
	 * for example to support those features with single lower squashfs.
	 * To avoid regressions in setups of overlay with re-formatted lower
	 * squashfs, do not allow decoding origin with lower null uuid unless
	 * user opted-in to one of the new features that require following the
	 * lower inode of non-dir upper.
	 */
	if (ovl_allow_offline_changes(ofs) && uuid_is_null(uuid))
		return false;

	for (i = 0; i < ofs->numfs; i++) {
		/*
		 * We use uuid to associate an overlay lower file handle with a
		 * lower layer, so we can accept lower fs with null uuid as long
		 * as all lower layers with null uuid are on the same fs.
		 * if we detect multiple lower fs with the same uuid, we
		 * disable lower file handle decoding on all of them.
		 */
		if (ofs->fs[i].is_lower &&
		    uuid_equal(&ofs->fs[i].sb->s_uuid, uuid)) {
			ofs->fs[i].bad_uuid = true;
			return false;
		}
	}
	return true;
}

/* Get a unique fsid for the layer */
static int ovl_get_fsid(struct ovl_fs *ofs, const struct path *path)
{
	struct super_block *sb = path->mnt->mnt_sb;
	unsigned int i;
	dev_t dev;
	int err;
	bool bad_uuid = false;
	bool warn = false;

	for (i = 0; i < ofs->numfs; i++) {
		if (ofs->fs[i].sb == sb)
			return i;
	}

	if (!ovl_lower_uuid_ok(ofs, &sb->s_uuid)) {
		bad_uuid = true;
		if (ofs->config.xino == OVL_XINO_AUTO) {
			ofs->config.xino = OVL_XINO_OFF;
			warn = true;
		}
		if (ofs->config.index || ofs->config.nfs_export) {
			ofs->config.index = false;
			ofs->config.nfs_export = false;
			warn = true;
		}
		if (warn) {
			pr_warn("%s uuid detected in lower fs '%pd2', falling back to xino=%s,index=off,nfs_export=off.\n",
				uuid_is_null(&sb->s_uuid) ? "null" :
							    "conflicting",
				path->dentry, ovl_xino_mode(&ofs->config));
		}
	}

	err = get_anon_bdev(&dev);
	if (err) {
		pr_err("failed to get anonymous bdev for lowerpath\n");
		return err;
	}

	ofs->fs[ofs->numfs].sb = sb;
	ofs->fs[ofs->numfs].pseudo_dev = dev;
	ofs->fs[ofs->numfs].bad_uuid = bad_uuid;

	return ofs->numfs++;
}

/*
 * The fsid after the last lower fsid is used for the data layers.
 * It is a "null fs" with a null sb, null uuid, and no pseudo dev.
 */
static int ovl_get_data_fsid(struct ovl_fs *ofs)
{
	return ofs->numfs;
}


static int ovl_get_layers(struct super_block *sb, struct ovl_fs *ofs,
			  struct ovl_fs_context *ctx, struct ovl_layer *layers)
{
	int err;
	unsigned int i;
	size_t nr_merged_lower;

	ofs->fs = kcalloc(ctx->nr + 2, sizeof(struct ovl_sb), GFP_KERNEL);
	if (ofs->fs == NULL)
		return -ENOMEM;

	/*
	 * idx/fsid 0 are reserved for upper fs even with lower only overlay
	 * and the last fsid is reserved for "null fs" of the data layers.
	 */
	ofs->numfs++;

	/*
	 * All lower layers that share the same fs as upper layer, use the same
	 * pseudo_dev as upper layer.  Allocate fs[0].pseudo_dev even for lower
	 * only overlay to simplify ovl_fs_free().
	 * is_lower will be set if upper fs is shared with a lower layer.
	 */
	err = get_anon_bdev(&ofs->fs[0].pseudo_dev);
	if (err) {
		pr_err("failed to get anonymous bdev for upper fs\n");
		return err;
	}

	if (ovl_upper_mnt(ofs)) {
		ofs->fs[0].sb = ovl_upper_mnt(ofs)->mnt_sb;
		ofs->fs[0].is_lower = false;
	}

	nr_merged_lower = ctx->nr - ctx->nr_data;
	for (i = 0; i < ctx->nr; i++) {
		struct ovl_fs_context_layer *l = &ctx->lower[i];
		struct vfsmount *mnt;
		struct inode *trap;
		int fsid;

		if (i < nr_merged_lower)
			fsid = ovl_get_fsid(ofs, &l->path);
		else
			fsid = ovl_get_data_fsid(ofs);
		if (fsid < 0)
			return fsid;

		/*
		 * Check if lower root conflicts with this overlay layers before
		 * checking if it is in-use as upperdir/workdir of "another"
		 * mount, because we do not bother to check in ovl_is_inuse() if
		 * the upperdir/workdir is in fact in-use by our
		 * upperdir/workdir.
		 */
		err = ovl_setup_trap(sb, l->path.dentry, &trap, "lowerdir");
		if (err)
			return err;

		if (ovl_is_inuse(l->path.dentry)) {
			err = ovl_report_in_use(ofs, "lowerdir");
			if (err) {
				iput(trap);
				return err;
			}
		}

		mnt = clone_private_mount(&l->path);
		err = PTR_ERR(mnt);
		if (IS_ERR(mnt)) {
			pr_err("failed to clone lowerpath\n");
			iput(trap);
			return err;
		}

		/*
		 * Make lower layers R/O.  That way fchmod/fchown on lower file
		 * will fail instead of modifying lower fs.
		 */
		mnt->mnt_flags |= MNT_READONLY | MNT_NOATIME;

		layers[ofs->numlayer].trap = trap;
		layers[ofs->numlayer].mnt = mnt;
		layers[ofs->numlayer].idx = ofs->numlayer;
		layers[ofs->numlayer].fsid = fsid;
		layers[ofs->numlayer].fs = &ofs->fs[fsid];
		layers[ofs->numlayer].name = l->name;
		l->name = NULL;
		ofs->numlayer++;
		ofs->fs[fsid].is_lower = true;
	}

	/*
	 * When all layers on same fs, overlay can use real inode numbers.
	 * With mount option "xino=<on|auto>", mounter declares that there are
	 * enough free high bits in underlying fs to hold the unique fsid.
	 * If overlayfs does encounter underlying inodes using the high xino
	 * bits reserved for fsid, it emits a warning and uses the original
	 * inode number or a non persistent inode number allocated from a
	 * dedicated range.
	 */
	if (ofs->numfs - !ovl_upper_mnt(ofs) == 1) {
		if (ofs->config.xino == OVL_XINO_ON)
			pr_info("\"xino=on\" is useless with all layers on same fs, ignore.\n");
		ofs->xino_mode = 0;
	} else if (ofs->config.xino == OVL_XINO_OFF) {
		ofs->xino_mode = -1;
	} else if (ofs->xino_mode < 0) {
		/*
		 * This is a roundup of number of bits needed for encoding
		 * fsid, where fsid 0 is reserved for upper fs (even with
		 * lower only overlay) +1 extra bit is reserved for the non
		 * persistent inode number range that is used for resolving
		 * xino lower bits overflow.
		 */
		BUILD_BUG_ON(ilog2(OVL_MAX_STACK) > 30);
		ofs->xino_mode = ilog2(ofs->numfs - 1) + 2;
	}

	if (ofs->xino_mode > 0) {
		pr_info("\"xino\" feature enabled using %d upper inode bits.\n",
			ofs->xino_mode);
	}

	return 0;
}

static struct ovl_entry *ovl_get_lowerstack(struct super_block *sb,
					    struct ovl_fs_context *ctx,
					    struct ovl_fs *ofs,
					    struct ovl_layer *layers)
{
	int err;
	unsigned int i;
	size_t nr_merged_lower;
	struct ovl_entry *oe;
	struct ovl_path *lowerstack;

	struct ovl_fs_context_layer *l;

	if (!ofs->config.upperdir && ctx->nr == 1) {
		pr_err("at least 2 lowerdir are needed while upperdir nonexistent\n");
		return ERR_PTR(-EINVAL);
	}

	err = -EINVAL;
	for (i = 0; i < ctx->nr; i++) {
		l = &ctx->lower[i];

		err = ovl_lower_dir(l->name, &l->path, ofs, &sb->s_stack_depth);
		if (err)
			return ERR_PTR(err);
	}

	err = -EINVAL;
	sb->s_stack_depth++;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("maximum fs stacking depth exceeded\n");
		return ERR_PTR(err);
	}

	err = ovl_get_layers(sb, ofs, ctx, layers);
	if (err)
		return ERR_PTR(err);

	err = -ENOMEM;
	/* Data-only layers are not merged in root directory */
	nr_merged_lower = ctx->nr - ctx->nr_data;
	oe = ovl_alloc_entry(nr_merged_lower);
	if (!oe)
		return ERR_PTR(err);

	lowerstack = ovl_lowerstack(oe);
	for (i = 0; i < nr_merged_lower; i++) {
		l = &ctx->lower[i];
		lowerstack[i].dentry = dget(l->path.dentry);
		lowerstack[i].layer = &ofs->layers[i + 1];
	}
	ofs->numdatalayer = ctx->nr_data;

	return oe;
}

/*
 * Check if this layer root is a descendant of:
 * - another layer of this overlayfs instance
 * - upper/work dir of any overlayfs instance
 */
static int ovl_check_layer(struct super_block *sb, struct ovl_fs *ofs,
			   struct dentry *dentry, const char *name,
			   bool is_lower)
{
	struct dentry *next = dentry, *parent;
	int err = 0;

	if (!dentry)
		return 0;

	parent = dget_parent(next);

	/* Walk back ancestors to root (inclusive) looking for traps */
	while (!err && parent != next) {
		if (is_lower && ovl_lookup_trap_inode(sb, parent)) {
			err = -ELOOP;
			pr_err("overlapping %s path\n", name);
		} else if (ovl_is_inuse(parent)) {
			err = ovl_report_in_use(ofs, name);
		}
		next = parent;
		parent = dget_parent(next);
		dput(next);
	}

	dput(parent);

	return err;
}

/*
 * Check if any of the layers or work dirs overlap.
 */
static int ovl_check_overlapping_layers(struct super_block *sb,
					struct ovl_fs *ofs)
{
	int i, err;

	if (ovl_upper_mnt(ofs)) {
		err = ovl_check_layer(sb, ofs, ovl_upper_mnt(ofs)->mnt_root,
				      "upperdir", false);
		if (err)
			return err;

		/*
		 * Checking workbasedir avoids hitting ovl_is_inuse(parent) of
		 * this instance and covers overlapping work and index dirs,
		 * unless work or index dir have been moved since created inside
		 * workbasedir.  In that case, we already have their traps in
		 * inode cache and we will catch that case on lookup.
		 */
		err = ovl_check_layer(sb, ofs, ofs->workbasedir, "workdir",
				      false);
		if (err)
			return err;
	}

	for (i = 1; i < ofs->numlayer; i++) {
		err = ovl_check_layer(sb, ofs,
				      ofs->layers[i].mnt->mnt_root,
				      "lowerdir", true);
		if (err)
			return err;
	}

	return 0;
}

static struct dentry *ovl_get_root(struct super_block *sb,
				   struct dentry *upperdentry,
				   struct ovl_entry *oe)
{
	struct dentry *root;
	struct ovl_path *lowerpath = ovl_lowerstack(oe);
	unsigned long ino = d_inode(lowerpath->dentry)->i_ino;
	int fsid = lowerpath->layer->fsid;
	struct ovl_inode_params oip = {
		.upperdentry = upperdentry,
		.oe = oe,
	};

	root = d_make_root(ovl_new_inode(sb, S_IFDIR, 0));
	if (!root)
		return NULL;

	if (upperdentry) {
		/* Root inode uses upper st_ino/i_ino */
		ino = d_inode(upperdentry)->i_ino;
		fsid = 0;
		ovl_dentry_set_upper_alias(root);
		if (ovl_is_impuredir(sb, upperdentry))
			ovl_set_flag(OVL_IMPURE, d_inode(root));
	}

	/* Root is always merge -> can have whiteouts */
	ovl_set_flag(OVL_WHITEOUTS, d_inode(root));
	ovl_dentry_set_flag(OVL_E_CONNECTED, root);
	ovl_set_upperdata(d_inode(root));
	ovl_inode_init(d_inode(root), &oip, ino, fsid);
	ovl_dentry_init_flags(root, upperdentry, oe, DCACHE_OP_WEAK_REVALIDATE);
	/* root keeps a reference of upperdentry */
	dget(upperdentry);

	return root;
}

static int ovl_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct ovl_fs_context *ctx = fc->fs_private;
	struct dentry *root_dentry;
	struct ovl_entry *oe;
	struct ovl_layer *layers;
	struct cred *cred;
	int err;

	err = -EIO;
	if (WARN_ON(fc->user_ns != current_user_ns()))
		goto out_err;

	sb->s_d_op = &ovl_dentry_operations;

	err = -ENOMEM;
	ofs->creator_cred = cred = prepare_creds();
	if (!cred)
		goto out_err;

	err = ovl_fs_params_verify(ctx, &ofs->config);
	if (err)
		goto out_err;

	err = -EINVAL;
	if (ctx->nr == 0) {
		if (!(fc->sb_flags & SB_SILENT))
			pr_err("missing 'lowerdir'\n");
		goto out_err;
	}

	err = -ENOMEM;
	layers = kcalloc(ctx->nr + 1, sizeof(struct ovl_layer), GFP_KERNEL);
	if (!layers)
		goto out_err;

	ofs->layers = layers;
	/* Layer 0 is reserved for upper even if there's no upper */
	ofs->numlayer = 1;

	sb->s_stack_depth = 0;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	atomic_long_set(&ofs->last_ino, 1);
	/* Assume underlying fs uses 32bit inodes unless proven otherwise */
	if (ofs->config.xino != OVL_XINO_OFF) {
		ofs->xino_mode = BITS_PER_LONG - 32;
		if (!ofs->xino_mode) {
			pr_warn("xino not supported on 32bit kernel, falling back to xino=off.\n");
			ofs->config.xino = OVL_XINO_OFF;
		}
	}

	/* alloc/destroy_inode needed for setting up traps in inode cache */
	sb->s_op = &ovl_super_operations;

	if (ofs->config.upperdir) {
		struct super_block *upper_sb;

		err = -EINVAL;
		if (!ofs->config.workdir) {
			pr_err("missing 'workdir'\n");
			goto out_err;
		}

		err = ovl_get_upper(sb, ofs, &layers[0], &ctx->upper);
		if (err)
			goto out_err;

		upper_sb = ovl_upper_mnt(ofs)->mnt_sb;
		if (!ovl_should_sync(ofs)) {
			ofs->errseq = errseq_sample(&upper_sb->s_wb_err);
			if (errseq_check(&upper_sb->s_wb_err, ofs->errseq)) {
				err = -EIO;
				pr_err("Cannot mount volatile when upperdir has an unseen error. Sync upperdir fs to clear state.\n");
				goto out_err;
			}
		}

		err = ovl_get_workdir(sb, ofs, &ctx->upper, &ctx->work);
		if (err)
			goto out_err;

		if (!ofs->workdir)
			sb->s_flags |= SB_RDONLY;

		sb->s_stack_depth = upper_sb->s_stack_depth;
		sb->s_time_gran = upper_sb->s_time_gran;
	}
	oe = ovl_get_lowerstack(sb, ctx, ofs, layers);
	err = PTR_ERR(oe);
	if (IS_ERR(oe))
		goto out_err;

	/* If the upper fs is nonexistent, we mark overlayfs r/o too */
	if (!ovl_upper_mnt(ofs))
		sb->s_flags |= SB_RDONLY;

	if (!ofs->config.uuid && ofs->numfs > 1) {
		pr_warn("The uuid=off requires a single fs for lower and upper, falling back to uuid=on.\n");
		ofs->config.uuid = true;
	}

	if (!ovl_force_readonly(ofs) && ofs->config.index) {
		err = ovl_get_indexdir(sb, ofs, oe, &ctx->upper);
		if (err)
			goto out_free_oe;

		/* Force r/o mount with no index dir */
		if (!ofs->indexdir)
			sb->s_flags |= SB_RDONLY;
	}

	err = ovl_check_overlapping_layers(sb, ofs);
	if (err)
		goto out_free_oe;

	/* Show index=off in /proc/mounts for forced r/o mount */
	if (!ofs->indexdir) {
		ofs->config.index = false;
		if (ovl_upper_mnt(ofs) && ofs->config.nfs_export) {
			pr_warn("NFS export requires an index dir, falling back to nfs_export=off.\n");
			ofs->config.nfs_export = false;
		}
	}

	if (ofs->config.metacopy && ofs->config.nfs_export) {
		pr_warn("NFS export is not supported with metadata only copy up, falling back to nfs_export=off.\n");
		ofs->config.nfs_export = false;
	}

	if (ofs->config.nfs_export)
		sb->s_export_op = &ovl_export_operations;

	/* Never override disk quota limits or use reserved space */
	cap_lower(cred->cap_effective, CAP_SYS_RESOURCE);

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_xattr = ofs->config.userxattr ? ovl_user_xattr_handlers :
		ovl_trusted_xattr_handlers;
	sb->s_fs_info = ofs;
	sb->s_flags |= SB_POSIXACL;
	sb->s_iflags |= SB_I_SKIP_SYNC;

	err = -ENOMEM;
	root_dentry = ovl_get_root(sb, ctx->upper.dentry, oe);
	if (!root_dentry)
		goto out_free_oe;

	sb->s_root = root_dentry;

	return 0;

out_free_oe:
	ovl_free_entry(oe);
out_err:
	ovl_free_fs(ofs);
	sb->s_fs_info = NULL;
	return err;
}

static int ovl_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, ovl_fill_super);
}

static inline void ovl_fs_context_free(struct ovl_fs_context *ctx)
{
	ovl_parse_param_drop_lowerdir(ctx);
	path_put(&ctx->upper);
	path_put(&ctx->work);
	kfree(ctx->lower);
	kfree(ctx);
}

static void ovl_free(struct fs_context *fc)
{
	struct ovl_fs *ofs = fc->s_fs_info;
	struct ovl_fs_context *ctx = fc->fs_private;

	/*
	 * ofs is stored in the fs_context when it is initialized.
	 * ofs is transferred to the superblock on a successful mount,
	 * but if an error occurs before the transfer we have to free
	 * it here.
	 */
	if (ofs)
		ovl_free_fs(ofs);

	if (ctx)
		ovl_fs_context_free(ctx);
}

static const struct fs_context_operations ovl_context_ops = {
	.parse_param = ovl_parse_param,
	.get_tree    = ovl_get_tree,
	.reconfigure = ovl_reconfigure,
	.free        = ovl_free,
};

/*
 * This is called during fsopen() and will record the user namespace of
 * the caller in fc->user_ns since we've raised FS_USERNS_MOUNT. We'll
 * need it when we actually create the superblock to verify that the
 * process creating the superblock is in the same user namespace as
 * process that called fsopen().
 */
static int ovl_init_fs_context(struct fs_context *fc)
{
	struct ovl_fs_context *ctx;
	struct ovl_fs *ofs;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL_ACCOUNT);
	if (!ctx)
		return -ENOMEM;

	/*
	 * By default we allocate for three lower layers. It's likely
	 * that it'll cover most users.
	 */
	ctx->lower = kmalloc_array(3, sizeof(*ctx->lower), GFP_KERNEL_ACCOUNT);
	if (!ctx->lower)
		goto out_err;
	ctx->capacity = 3;

	ofs = kzalloc(sizeof(struct ovl_fs), GFP_KERNEL);
	if (!ofs)
		goto out_err;

	ofs->config.redirect_mode = ovl_redirect_mode_def();
	ofs->config.index	= ovl_index_def;
	ofs->config.uuid	= true;
	ofs->config.nfs_export	= ovl_nfs_export_def;
	ofs->config.xino	= ovl_xino_def();
	ofs->config.metacopy	= ovl_metacopy_def;

	fc->s_fs_info		= ofs;
	fc->fs_private		= ctx;
	fc->ops			= &ovl_context_ops;
	return 0;

out_err:
	ovl_fs_context_free(ctx);
	return -ENOMEM;

}

static struct file_system_type ovl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "overlay",
	.init_fs_context	= ovl_init_fs_context,
	.parameters		= ovl_parameter_spec,
	.fs_flags		= FS_USERNS_MOUNT,
	.kill_sb		= kill_anon_super,
};
MODULE_ALIAS_FS("overlay");

static void ovl_inode_init_once(void *foo)
{
	struct ovl_inode *oi = foo;

	inode_init_once(&oi->vfs_inode);
}

static int __init ovl_init(void)
{
	int err;

	ovl_inode_cachep = kmem_cache_create("ovl_inode",
					     sizeof(struct ovl_inode), 0,
					     (SLAB_RECLAIM_ACCOUNT|
					      SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     ovl_inode_init_once);
	if (ovl_inode_cachep == NULL)
		return -ENOMEM;

	err = ovl_aio_request_cache_init();
	if (!err) {
		err = register_filesystem(&ovl_fs_type);
		if (!err)
			return 0;

		ovl_aio_request_cache_destroy();
	}
	kmem_cache_destroy(ovl_inode_cachep);

	return err;
}

static void __exit ovl_exit(void)
{
	unregister_filesystem(&ovl_fs_type);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ovl_inode_cachep);
	ovl_aio_request_cache_destroy();
}

module_init(ovl_init);
module_exit(ovl_exit);
