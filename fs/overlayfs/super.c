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
#include "overlayfs.h"

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Overlay filesystem");
MODULE_LICENSE("GPL");


struct ovl_dir_cache;

#define OVL_MAX_STACK 500

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

static void ovl_entry_stack_free(struct ovl_entry *oe)
{
	unsigned int i;

	for (i = 0; i < oe->numlower; i++)
		dput(oe->lowerstack[i].dentry);
}

static bool ovl_metacopy_def = IS_ENABLED(CONFIG_OVERLAY_FS_METACOPY);
module_param_named(metacopy, ovl_metacopy_def, bool, 0644);
MODULE_PARM_DESC(metacopy,
		 "Default to on or off for the metadata only copy up feature");

static void ovl_dentry_release(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe) {
		ovl_entry_stack_free(oe);
		kfree_rcu(oe, rcu);
	}
}

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
	struct ovl_entry *oe = dentry->d_fsdata;
	struct dentry *upper;
	unsigned int i;
	int ret = 1;

	upper = ovl_dentry_upper(dentry);
	if (upper)
		ret = ovl_revalidate_real(upper, flags, weak);

	for (i = 0; ret > 0 && i < oe->numlower; i++) {
		ret = ovl_revalidate_real(oe->lowerstack[i].dentry, flags,
					  weak);
	}
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
	.d_release = ovl_dentry_release,
	.d_real = ovl_d_real,
	.d_revalidate = ovl_dentry_revalidate,
	.d_weak_revalidate = ovl_dentry_weak_revalidate,
};

static struct kmem_cache *ovl_inode_cachep;

static struct inode *ovl_alloc_inode(struct super_block *sb)
{
	struct ovl_inode *oi = kmem_cache_alloc(ovl_inode_cachep, GFP_KERNEL);

	if (!oi)
		return NULL;

	oi->cache = NULL;
	oi->redirect = NULL;
	oi->version = 0;
	oi->flags = 0;
	oi->__upperdentry = NULL;
	oi->lower = NULL;
	oi->lowerdata = NULL;
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
	iput(oi->lower);
	if (S_ISDIR(inode->i_mode))
		ovl_dir_cache_free(inode);
	else
		iput(oi->lowerdata);
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
	}
	kern_unmount_array(mounts, ofs->numlayer);
	kfree(ofs->layers);
	for (i = 0; i < ofs->numfs; i++)
		free_anon_bdev(ofs->fs[i].pseudo_dev);
	kfree(ofs->fs);

	kfree(ofs->config.lowerdir);
	kfree(ofs->config.upperdir);
	kfree(ofs->config.workdir);
	kfree(ofs->config.redirect_mode);
	if (ofs->creator_cred)
		put_cred(ofs->creator_cred);
	kfree(ofs);
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

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
 * @sb: The overlayfs super block
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

static const char *ovl_redirect_mode_def(void)
{
	return ovl_redirect_dir_def ? "on" : "off";
}

static const char * const ovl_xino_str[] = {
	"off",
	"auto",
	"on",
};

static inline int ovl_xino_def(void)
{
	return ovl_xino_auto_def ? OVL_XINO_AUTO : OVL_XINO_OFF;
}

/**
 * ovl_show_options
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does not fail.
 */
static int ovl_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct ovl_fs *ofs = sb->s_fs_info;

	seq_show_option(m, "lowerdir", ofs->config.lowerdir);
	if (ofs->config.upperdir) {
		seq_show_option(m, "upperdir", ofs->config.upperdir);
		seq_show_option(m, "workdir", ofs->config.workdir);
	}
	if (ofs->config.default_permissions)
		seq_puts(m, ",default_permissions");
	if (strcmp(ofs->config.redirect_mode, ovl_redirect_mode_def()) != 0)
		seq_printf(m, ",redirect_dir=%s", ofs->config.redirect_mode);
	if (ofs->config.index != ovl_index_def)
		seq_printf(m, ",index=%s", ofs->config.index ? "on" : "off");
	if (!ofs->config.uuid)
		seq_puts(m, ",uuid=off");
	if (ofs->config.nfs_export != ovl_nfs_export_def)
		seq_printf(m, ",nfs_export=%s", ofs->config.nfs_export ?
						"on" : "off");
	if (ofs->config.xino != ovl_xino_def() && !ovl_same_fs(sb))
		seq_printf(m, ",xino=%s", ovl_xino_str[ofs->config.xino]);
	if (ofs->config.metacopy != ovl_metacopy_def)
		seq_printf(m, ",metacopy=%s",
			   ofs->config.metacopy ? "on" : "off");
	if (ofs->config.ovl_volatile)
		seq_puts(m, ",volatile");
	if (ofs->config.userxattr)
		seq_puts(m, ",userxattr");
	return 0;
}

static int ovl_remount(struct super_block *sb, int *flags, char *data)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct super_block *upper_sb;
	int ret = 0;

	if (!(*flags & SB_RDONLY) && ovl_force_readonly(ofs))
		return -EROFS;

	if (*flags & SB_RDONLY && !sb_rdonly(sb)) {
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
	.remount_fs	= ovl_remount,
};

enum {
	OPT_LOWERDIR,
	OPT_UPPERDIR,
	OPT_WORKDIR,
	OPT_DEFAULT_PERMISSIONS,
	OPT_REDIRECT_DIR,
	OPT_INDEX_ON,
	OPT_INDEX_OFF,
	OPT_UUID_ON,
	OPT_UUID_OFF,
	OPT_NFS_EXPORT_ON,
	OPT_USERXATTR,
	OPT_NFS_EXPORT_OFF,
	OPT_XINO_ON,
	OPT_XINO_OFF,
	OPT_XINO_AUTO,
	OPT_METACOPY_ON,
	OPT_METACOPY_OFF,
	OPT_VOLATILE,
	OPT_ERR,
};

static const match_table_t ovl_tokens = {
	{OPT_LOWERDIR,			"lowerdir=%s"},
	{OPT_UPPERDIR,			"upperdir=%s"},
	{OPT_WORKDIR,			"workdir=%s"},
	{OPT_DEFAULT_PERMISSIONS,	"default_permissions"},
	{OPT_REDIRECT_DIR,		"redirect_dir=%s"},
	{OPT_INDEX_ON,			"index=on"},
	{OPT_INDEX_OFF,			"index=off"},
	{OPT_USERXATTR,			"userxattr"},
	{OPT_UUID_ON,			"uuid=on"},
	{OPT_UUID_OFF,			"uuid=off"},
	{OPT_NFS_EXPORT_ON,		"nfs_export=on"},
	{OPT_NFS_EXPORT_OFF,		"nfs_export=off"},
	{OPT_XINO_ON,			"xino=on"},
	{OPT_XINO_OFF,			"xino=off"},
	{OPT_XINO_AUTO,			"xino=auto"},
	{OPT_METACOPY_ON,		"metacopy=on"},
	{OPT_METACOPY_OFF,		"metacopy=off"},
	{OPT_VOLATILE,			"volatile"},
	{OPT_ERR,			NULL}
};

static char *ovl_next_opt(char **s)
{
	char *sbegin = *s;
	char *p;

	if (sbegin == NULL)
		return NULL;

	for (p = sbegin; *p; p++) {
		if (*p == '\\') {
			p++;
			if (!*p)
				break;
		} else if (*p == ',') {
			*p = '\0';
			*s = p + 1;
			return sbegin;
		}
	}
	*s = NULL;
	return sbegin;
}

static int ovl_parse_redirect_mode(struct ovl_config *config, const char *mode)
{
	if (strcmp(mode, "on") == 0) {
		config->redirect_dir = true;
		/*
		 * Does not make sense to have redirect creation without
		 * redirect following.
		 */
		config->redirect_follow = true;
	} else if (strcmp(mode, "follow") == 0) {
		config->redirect_follow = true;
	} else if (strcmp(mode, "off") == 0) {
		if (ovl_redirect_always_follow)
			config->redirect_follow = true;
	} else if (strcmp(mode, "nofollow") != 0) {
		pr_err("bad mount option \"redirect_dir=%s\"\n",
		       mode);
		return -EINVAL;
	}

	return 0;
}

static int ovl_parse_opt(char *opt, struct ovl_config *config)
{
	char *p;
	int err;
	bool metacopy_opt = false, redirect_opt = false;
	bool nfs_export_opt = false, index_opt = false;

	config->redirect_mode = kstrdup(ovl_redirect_mode_def(), GFP_KERNEL);
	if (!config->redirect_mode)
		return -ENOMEM;

	while ((p = ovl_next_opt(&opt)) != NULL) {
		int token;
		substring_t args[MAX_OPT_ARGS];

		if (!*p)
			continue;

		token = match_token(p, ovl_tokens, args);
		switch (token) {
		case OPT_UPPERDIR:
			kfree(config->upperdir);
			config->upperdir = match_strdup(&args[0]);
			if (!config->upperdir)
				return -ENOMEM;
			break;

		case OPT_LOWERDIR:
			kfree(config->lowerdir);
			config->lowerdir = match_strdup(&args[0]);
			if (!config->lowerdir)
				return -ENOMEM;
			break;

		case OPT_WORKDIR:
			kfree(config->workdir);
			config->workdir = match_strdup(&args[0]);
			if (!config->workdir)
				return -ENOMEM;
			break;

		case OPT_DEFAULT_PERMISSIONS:
			config->default_permissions = true;
			break;

		case OPT_REDIRECT_DIR:
			kfree(config->redirect_mode);
			config->redirect_mode = match_strdup(&args[0]);
			if (!config->redirect_mode)
				return -ENOMEM;
			redirect_opt = true;
			break;

		case OPT_INDEX_ON:
			config->index = true;
			index_opt = true;
			break;

		case OPT_INDEX_OFF:
			config->index = false;
			index_opt = true;
			break;

		case OPT_UUID_ON:
			config->uuid = true;
			break;

		case OPT_UUID_OFF:
			config->uuid = false;
			break;

		case OPT_NFS_EXPORT_ON:
			config->nfs_export = true;
			nfs_export_opt = true;
			break;

		case OPT_NFS_EXPORT_OFF:
			config->nfs_export = false;
			nfs_export_opt = true;
			break;

		case OPT_XINO_ON:
			config->xino = OVL_XINO_ON;
			break;

		case OPT_XINO_OFF:
			config->xino = OVL_XINO_OFF;
			break;

		case OPT_XINO_AUTO:
			config->xino = OVL_XINO_AUTO;
			break;

		case OPT_METACOPY_ON:
			config->metacopy = true;
			metacopy_opt = true;
			break;

		case OPT_METACOPY_OFF:
			config->metacopy = false;
			metacopy_opt = true;
			break;

		case OPT_VOLATILE:
			config->ovl_volatile = true;
			break;

		case OPT_USERXATTR:
			config->userxattr = true;
			break;

		default:
			pr_err("unrecognized mount option \"%s\" or missing value\n",
					p);
			return -EINVAL;
		}
	}

	/* Workdir/index are useless in non-upper mount */
	if (!config->upperdir) {
		if (config->workdir) {
			pr_info("option \"workdir=%s\" is useless in a non-upper mount, ignore\n",
				config->workdir);
			kfree(config->workdir);
			config->workdir = NULL;
		}
		if (config->index && index_opt) {
			pr_info("option \"index=on\" is useless in a non-upper mount, ignore\n");
			index_opt = false;
		}
		config->index = false;
	}

	if (!config->upperdir && config->ovl_volatile) {
		pr_info("option \"volatile\" is meaningless in a non-upper mount, ignoring it.\n");
		config->ovl_volatile = false;
	}

	err = ovl_parse_redirect_mode(config, config->redirect_mode);
	if (err)
		return err;

	/*
	 * This is to make the logic below simpler.  It doesn't make any other
	 * difference, since config->redirect_dir is only used for upper.
	 */
	if (!config->upperdir && config->redirect_follow)
		config->redirect_dir = true;

	/* Resolve metacopy -> redirect_dir dependency */
	if (config->metacopy && !config->redirect_dir) {
		if (metacopy_opt && redirect_opt) {
			pr_err("conflicting options: metacopy=on,redirect_dir=%s\n",
			       config->redirect_mode);
			return -EINVAL;
		}
		if (redirect_opt) {
			/*
			 * There was an explicit redirect_dir=... that resulted
			 * in this conflict.
			 */
			pr_info("disabling metacopy due to redirect_dir=%s\n",
				config->redirect_mode);
			config->metacopy = false;
		} else {
			/* Automatically enable redirect otherwise. */
			config->redirect_follow = config->redirect_dir = true;
		}
	}

	/* Resolve nfs_export -> index dependency */
	if (config->nfs_export && !config->index) {
		if (!config->upperdir && config->redirect_follow) {
			pr_info("NFS export requires \"redirect_dir=nofollow\" on non-upper mount, falling back to nfs_export=off.\n");
			config->nfs_export = false;
		} else if (nfs_export_opt && index_opt) {
			pr_err("conflicting options: nfs_export=on,index=off\n");
			return -EINVAL;
		} else if (index_opt) {
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
		if (nfs_export_opt && metacopy_opt) {
			pr_err("conflicting options: nfs_export=on,metacopy=on\n");
			return -EINVAL;
		}
		if (metacopy_opt) {
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
		if (config->redirect_follow && redirect_opt) {
			pr_err("conflicting options: userxattr,redirect_dir=%s\n",
			       config->redirect_mode);
			return -EINVAL;
		}
		if (config->metacopy && metacopy_opt) {
			pr_err("conflicting options: userxattr,metacopy=on\n");
			return -EINVAL;
		}
		/*
		 * Silently disable default setting of redirect and metacopy.
		 * This shall be the default in the future as well: these
		 * options must be explicitly enabled if used together with
		 * userxattr.
		 */
		config->redirect_dir = config->redirect_follow = false;
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
	work = lookup_one_len(name, ofs->workbasedir, strlen(name));

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
			err = ovl_workdir_cleanup(dir, mnt, work, 0);
			dput(work);
			if (err == -EINVAL) {
				work = ERR_PTR(err);
				goto out_unlock;
			}
			goto retry;
		}

		work = ovl_create_real(dir, work, OVL_CATTR(attr.ia_mode));
		err = PTR_ERR(work);
		if (IS_ERR(work))
			goto out_err;

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
		err = vfs_removexattr(&init_user_ns, work,
				      XATTR_NAME_POSIX_ACL_DEFAULT);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		err = vfs_removexattr(&init_user_ns, work,
				      XATTR_NAME_POSIX_ACL_ACCESS);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		/* Clear any inherited mode bits */
		inode_lock(work->d_inode);
		err = notify_change(&init_user_ns, work, &attr, NULL);
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

static void ovl_unescape(char *s)
{
	char *d = s;

	for (;; s++, d++) {
		if (*s == '\\')
			s++;
		*d = *s;
		if (!*s)
			break;
	}
}

static int ovl_mount_dir_noesc(const char *name, struct path *path)
{
	int err = -EINVAL;

	if (!*name) {
		pr_err("empty lowerdir\n");
		goto out;
	}
	err = kern_path(name, LOOKUP_FOLLOW, path);
	if (err) {
		pr_err("failed to resolve '%s': %i\n", name, err);
		goto out;
	}
	err = -EINVAL;
	if (ovl_dentry_weird(path->dentry)) {
		pr_err("filesystem on '%s' not supported\n", name);
		goto out_put;
	}
	if (mnt_user_ns(path->mnt) != &init_user_ns) {
		pr_err("idmapped layers are currently not supported\n");
		goto out_put;
	}
	if (!d_is_dir(path->dentry)) {
		pr_err("'%s' not a directory\n", name);
		goto out_put;
	}
	return 0;

out_put:
	path_put_init(path);
out:
	return err;
}

static int ovl_mount_dir(const char *name, struct path *path)
{
	int err = -ENOMEM;
	char *tmp = kstrdup(name, GFP_KERNEL);

	if (tmp) {
		ovl_unescape(tmp);
		err = ovl_mount_dir_noesc(tmp, path);

		if (!err && path->dentry->d_flags & DCACHE_OP_REAL) {
			pr_err("filesystem on '%s' not supported as upperdir\n",
			       tmp);
			path_put_init(path);
			err = -EINVAL;
		}
		kfree(tmp);
	}
	return err;
}

static int ovl_check_namelen(struct path *path, struct ovl_fs *ofs,
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

	err = ovl_mount_dir_noesc(name, path);
	if (err)
		return err;

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

static unsigned int ovl_split_lowerdirs(char *str)
{
	unsigned int ctr = 1;
	char *s, *d;

	for (s = d = str;; s++, d++) {
		if (*s == '\\') {
			s++;
		} else if (*s == ':') {
			*d = '\0';
			ctr++;
			continue;
		}
		*d = *s;
		if (!*s)
			break;
	}
	return ctr;
}

static int __maybe_unused
ovl_posix_acl_xattr_get(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, inode, handler->name, buffer, size);
}

static int __maybe_unused
ovl_posix_acl_xattr_set(const struct xattr_handler *handler,
			struct user_namespace *mnt_userns,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *value,
			size_t size, int flags)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct inode *realinode = ovl_inode_real(inode);
	struct posix_acl *acl = NULL;
	int err;

	/* Check that everything is OK before copy-up */
	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	err = -EOPNOTSUPP;
	if (!IS_POSIXACL(d_inode(workdir)))
		goto out_acl_release;
	if (!realinode->i_op->set_acl)
		goto out_acl_release;
	if (handler->flags == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode)) {
		err = acl ? -EACCES : 0;
		goto out_acl_release;
	}
	err = -EPERM;
	if (!inode_owner_or_capable(&init_user_ns, inode))
		goto out_acl_release;

	posix_acl_release(acl);

	/*
	 * Check if sgid bit needs to be cleared (actual setacl operation will
	 * be done with mounter's capabilities and so that won't do it for us).
	 */
	if (unlikely(inode->i_mode & S_ISGID) &&
	    handler->flags == ACL_TYPE_ACCESS &&
	    !in_group_p(inode->i_gid) &&
	    !capable_wrt_inode_uidgid(&init_user_ns, inode, CAP_FSETID)) {
		struct iattr iattr = { .ia_valid = ATTR_KILL_SGID };

		err = ovl_setattr(&init_user_ns, dentry, &iattr);
		if (err)
			return err;
	}

	err = ovl_xattr_set(dentry, inode, handler->name, value, size, flags);
	return err;

out_acl_release:
	posix_acl_release(acl);
	return err;
}

static int ovl_own_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static int ovl_own_xattr_set(const struct xattr_handler *handler,
			     struct user_namespace *mnt_userns,
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
			       struct user_namespace *mnt_userns,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, const void *value,
			       size_t size, int flags)
{
	return ovl_xattr_set(dentry, inode, name, value, size, flags);
}

static const struct xattr_handler __maybe_unused
ovl_posix_acl_access_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_ACCESS,
	.flags = ACL_TYPE_ACCESS,
	.get = ovl_posix_acl_xattr_get,
	.set = ovl_posix_acl_xattr_set,
};

static const struct xattr_handler __maybe_unused
ovl_posix_acl_default_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_DEFAULT,
	.flags = ACL_TYPE_DEFAULT,
	.get = ovl_posix_acl_xattr_get,
	.set = ovl_posix_acl_xattr_set,
};

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
#ifdef CONFIG_FS_POSIX_ACL
	&ovl_posix_acl_access_xattr_handler,
	&ovl_posix_acl_default_xattr_handler,
#endif
	&ovl_own_trusted_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static const struct xattr_handler *ovl_user_xattr_handlers[] = {
#ifdef CONFIG_FS_POSIX_ACL
	&ovl_posix_acl_access_xattr_handler,
	&ovl_posix_acl_default_xattr_handler,
#endif
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
			 struct ovl_layer *upper_layer, struct path *upperpath)
{
	struct vfsmount *upper_mnt;
	int err;

	err = ovl_mount_dir(ofs->config.upperdir, upperpath);
	if (err)
		goto out;

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
static int ovl_check_rename_whiteout(struct dentry *workdir)
{
	struct inode *dir = d_inode(workdir);
	struct dentry *temp;
	struct dentry *dest;
	struct dentry *whiteout;
	struct name_snapshot name;
	int err;

	inode_lock_nested(dir, I_MUTEX_PARENT);

	temp = ovl_create_temp(workdir, OVL_CATTR(S_IFREG | 0));
	err = PTR_ERR(temp);
	if (IS_ERR(temp))
		goto out_unlock;

	dest = ovl_lookup_temp(workdir);
	err = PTR_ERR(dest);
	if (IS_ERR(dest)) {
		dput(temp);
		goto out_unlock;
	}

	/* Name is inline and stable - using snapshot as a copy helper */
	take_dentry_name_snapshot(&name, temp);
	err = ovl_do_rename(dir, temp, dir, dest, RENAME_WHITEOUT);
	if (err) {
		if (err == -EINVAL)
			err = 0;
		goto cleanup_temp;
	}

	whiteout = lookup_one_len(name.name.name, workdir, name.name.len);
	err = PTR_ERR(whiteout);
	if (IS_ERR(whiteout))
		goto cleanup_temp;

	err = ovl_is_whiteout(whiteout);

	/* Best effort cleanup of whiteout and temp file */
	if (err)
		ovl_cleanup(dir, whiteout);
	dput(whiteout);

cleanup_temp:
	ovl_cleanup(dir, temp);
	release_dentry_name_snapshot(&name);
	dput(temp);
	dput(dest);

out_unlock:
	inode_unlock(dir);

	return err;
}

static struct dentry *ovl_lookup_or_create(struct dentry *parent,
					   const char *name, umode_t mode)
{
	size_t len = strlen(name);
	struct dentry *child;

	inode_lock_nested(parent->d_inode, I_MUTEX_PARENT);
	child = lookup_one_len(name, parent, len);
	if (!IS_ERR(child) && !child->d_inode)
		child = ovl_create_real(parent->d_inode, child,
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
		d = ovl_lookup_or_create(d, *name, ctr > 1 ? S_IFDIR : S_IFREG);
		if (IS_ERR(d))
			return PTR_ERR(d);
	}
	dput(d);
	return 0;
}

static int ovl_make_workdir(struct super_block *sb, struct ovl_fs *ofs,
			    struct path *workpath)
{
	struct vfsmount *mnt = ovl_upper_mnt(ofs);
	struct dentry *temp, *workdir;
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
	temp = ovl_do_tmpfile(ofs->workdir, S_IFREG | 0);
	ofs->tmpfile = !IS_ERR(temp);
	if (ofs->tmpfile)
		dput(temp);
	else
		pr_warn("upper fs does not support tmpfile.\n");


	/* Check if upper/work fs supports RENAME_WHITEOUT */
	err = ovl_check_rename_whiteout(ofs->workdir);
	if (err < 0)
		goto out;

	rename_whiteout = err;
	if (!rename_whiteout)
		pr_warn("upper fs does not support RENAME_WHITEOUT.\n");

	/*
	 * Check if upper/work fs supports (trusted|user).overlay.* xattr
	 */
	err = ovl_do_setxattr(ofs, ofs->workdir, OVL_XATTR_OPAQUE, "0", 1);
	if (err) {
		ofs->noxattr = true;
		if (ofs->config.index || ofs->config.metacopy) {
			ofs->config.index = false;
			ofs->config.metacopy = false;
			pr_warn("upper fs does not support xattr, falling back to index=off,metacopy=off.\n");
		}
		/*
		 * xattr support is required for persistent st_ino.
		 * Without persistent st_ino, xino=auto falls back to xino=off.
		 */
		if (ofs->config.xino == OVL_XINO_AUTO) {
			ofs->config.xino = OVL_XINO_OFF;
			pr_warn("upper fs does not support xattr, falling back to xino=off.\n");
		}
		err = 0;
	} else {
		ovl_do_removexattr(ofs, ofs->workdir, OVL_XATTR_OPAQUE);
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
			   struct path *upperpath)
{
	int err;
	struct path workpath = { };

	err = ovl_mount_dir(ofs->config.workdir, &workpath);
	if (err)
		goto out;

	err = -EINVAL;
	if (upperpath->mnt != workpath.mnt) {
		pr_err("workdir and upperdir must reside under the same mount\n");
		goto out;
	}
	if (!ovl_workdir_ok(workpath.dentry, upperpath->dentry)) {
		pr_err("workdir and upperdir must be separate subtrees\n");
		goto out;
	}

	ofs->workbasedir = dget(workpath.dentry);

	if (ovl_inuse_trylock(ofs->workbasedir)) {
		ofs->workdir_locked = true;
	} else {
		err = ovl_report_in_use(ofs, "workdir");
		if (err)
			goto out;
	}

	err = ovl_setup_trap(sb, ofs->workbasedir, &ofs->workbasedir_trap,
			     "workdir");
	if (err)
		goto out;

	err = ovl_make_workdir(sb, ofs, &workpath);

out:
	path_put(&workpath);

	return err;
}

static int ovl_get_indexdir(struct super_block *sb, struct ovl_fs *ofs,
			    struct ovl_entry *oe, struct path *upperpath)
{
	struct vfsmount *mnt = ovl_upper_mnt(ofs);
	struct dentry *indexdir;
	int err;

	err = mnt_want_write(mnt);
	if (err)
		return err;

	/* Verify lower root is upper root origin */
	err = ovl_verify_origin(ofs, upperpath->dentry,
				oe->lowerstack[0].dentry, true);
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
	if (!ofs->config.index && !ofs->config.metacopy &&
	    ofs->config.xino != OVL_XINO_ON &&
	    uuid_is_null(uuid))
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
				path->dentry, ovl_xino_str[ofs->config.xino]);
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

static int ovl_get_layers(struct super_block *sb, struct ovl_fs *ofs,
			  struct path *stack, unsigned int numlower,
			  struct ovl_layer *layers)
{
	int err;
	unsigned int i;

	err = -ENOMEM;
	ofs->fs = kcalloc(numlower + 1, sizeof(struct ovl_sb), GFP_KERNEL);
	if (ofs->fs == NULL)
		goto out;

	/* idx/fsid 0 are reserved for upper fs even with lower only overlay */
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
		goto out;
	}

	if (ovl_upper_mnt(ofs)) {
		ofs->fs[0].sb = ovl_upper_mnt(ofs)->mnt_sb;
		ofs->fs[0].is_lower = false;
	}

	for (i = 0; i < numlower; i++) {
		struct vfsmount *mnt;
		struct inode *trap;
		int fsid;

		err = fsid = ovl_get_fsid(ofs, &stack[i]);
		if (err < 0)
			goto out;

		/*
		 * Check if lower root conflicts with this overlay layers before
		 * checking if it is in-use as upperdir/workdir of "another"
		 * mount, because we do not bother to check in ovl_is_inuse() if
		 * the upperdir/workdir is in fact in-use by our
		 * upperdir/workdir.
		 */
		err = ovl_setup_trap(sb, stack[i].dentry, &trap, "lowerdir");
		if (err)
			goto out;

		if (ovl_is_inuse(stack[i].dentry)) {
			err = ovl_report_in_use(ofs, "lowerdir");
			if (err) {
				iput(trap);
				goto out;
			}
		}

		mnt = clone_private_mount(&stack[i]);
		err = PTR_ERR(mnt);
		if (IS_ERR(mnt)) {
			pr_err("failed to clone lowerpath\n");
			iput(trap);
			goto out;
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

	err = 0;
out:
	return err;
}

static struct ovl_entry *ovl_get_lowerstack(struct super_block *sb,
				const char *lower, unsigned int numlower,
				struct ovl_fs *ofs, struct ovl_layer *layers)
{
	int err;
	struct path *stack = NULL;
	unsigned int i;
	struct ovl_entry *oe;

	if (!ofs->config.upperdir && numlower == 1) {
		pr_err("at least 2 lowerdir are needed while upperdir nonexistent\n");
		return ERR_PTR(-EINVAL);
	}

	stack = kcalloc(numlower, sizeof(struct path), GFP_KERNEL);
	if (!stack)
		return ERR_PTR(-ENOMEM);

	err = -EINVAL;
	for (i = 0; i < numlower; i++) {
		err = ovl_lower_dir(lower, &stack[i], ofs, &sb->s_stack_depth);
		if (err)
			goto out_err;

		lower = strchr(lower, '\0') + 1;
	}

	err = -EINVAL;
	sb->s_stack_depth++;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("maximum fs stacking depth exceeded\n");
		goto out_err;
	}

	err = ovl_get_layers(sb, ofs, stack, numlower, layers);
	if (err)
		goto out_err;

	err = -ENOMEM;
	oe = ovl_alloc_entry(numlower);
	if (!oe)
		goto out_err;

	for (i = 0; i < numlower; i++) {
		oe->lowerstack[i].dentry = dget(stack[i].dentry);
		oe->lowerstack[i].layer = &ofs->layers[i+1];
	}

out:
	for (i = 0; i < numlower; i++)
		path_put(&stack[i]);
	kfree(stack);

	return oe;

out_err:
	oe = ERR_PTR(err);
	goto out;
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
	struct ovl_path *lowerpath = &oe->lowerstack[0];
	unsigned long ino = d_inode(lowerpath->dentry)->i_ino;
	int fsid = lowerpath->layer->fsid;
	struct ovl_inode_params oip = {
		.upperdentry = upperdentry,
		.lowerpath = lowerpath,
	};

	root = d_make_root(ovl_new_inode(sb, S_IFDIR, 0));
	if (!root)
		return NULL;

	root->d_fsdata = oe;

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
	ovl_dentry_update_reval(root, upperdentry, DCACHE_OP_WEAK_REVALIDATE);

	return root;
}

static int ovl_fill_super(struct super_block *sb, void *data, int silent)
{
	struct path upperpath = { };
	struct dentry *root_dentry;
	struct ovl_entry *oe;
	struct ovl_fs *ofs;
	struct ovl_layer *layers;
	struct cred *cred;
	char *splitlower = NULL;
	unsigned int numlower;
	int err;

	err = -EIO;
	if (WARN_ON(sb->s_user_ns != current_user_ns()))
		goto out;

	sb->s_d_op = &ovl_dentry_operations;

	err = -ENOMEM;
	ofs = kzalloc(sizeof(struct ovl_fs), GFP_KERNEL);
	if (!ofs)
		goto out;

	err = -ENOMEM;
	ofs->creator_cred = cred = prepare_creds();
	if (!cred)
		goto out_err;

	/* Is there a reason anyone would want not to share whiteouts? */
	ofs->share_whiteout = true;

	ofs->config.index = ovl_index_def;
	ofs->config.uuid = true;
	ofs->config.nfs_export = ovl_nfs_export_def;
	ofs->config.xino = ovl_xino_def();
	ofs->config.metacopy = ovl_metacopy_def;
	err = ovl_parse_opt((char *) data, &ofs->config);
	if (err)
		goto out_err;

	err = -EINVAL;
	if (!ofs->config.lowerdir) {
		if (!silent)
			pr_err("missing 'lowerdir'\n");
		goto out_err;
	}

	err = -ENOMEM;
	splitlower = kstrdup(ofs->config.lowerdir, GFP_KERNEL);
	if (!splitlower)
		goto out_err;

	err = -EINVAL;
	numlower = ovl_split_lowerdirs(splitlower);
	if (numlower > OVL_MAX_STACK) {
		pr_err("too many lower directories, limit is %d\n",
		       OVL_MAX_STACK);
		goto out_err;
	}

	err = -ENOMEM;
	layers = kcalloc(numlower + 1, sizeof(struct ovl_layer), GFP_KERNEL);
	if (!layers)
		goto out_err;

	ofs->layers = layers;
	/* Layer 0 is reserved for upper even if there's no upper */
	ofs->numlayer = 1;

	sb->s_stack_depth = 0;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	atomic_long_set(&ofs->last_ino, 1);
	/* Assume underlaying fs uses 32bit inodes unless proven otherwise */
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

		err = ovl_get_upper(sb, ofs, &layers[0], &upperpath);
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

		err = ovl_get_workdir(sb, ofs, &upperpath);
		if (err)
			goto out_err;

		if (!ofs->workdir)
			sb->s_flags |= SB_RDONLY;

		sb->s_stack_depth = upper_sb->s_stack_depth;
		sb->s_time_gran = upper_sb->s_time_gran;
	}
	oe = ovl_get_lowerstack(sb, splitlower, numlower, ofs, layers);
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
		err = ovl_get_indexdir(sb, ofs, oe, &upperpath);
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
	root_dentry = ovl_get_root(sb, upperpath.dentry, oe);
	if (!root_dentry)
		goto out_free_oe;

	mntput(upperpath.mnt);
	kfree(splitlower);

	sb->s_root = root_dentry;

	return 0;

out_free_oe:
	ovl_entry_stack_free(oe);
	kfree(oe);
out_err:
	kfree(splitlower);
	path_put(&upperpath);
	ovl_free_fs(ofs);
out:
	return err;
}

static struct dentry *ovl_mount(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *raw_data)
{
	return mount_nodev(fs_type, flags, raw_data, ovl_fill_super);
}

static struct file_system_type ovl_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "overlay",
	.fs_flags	= FS_USERNS_MOUNT,
	.mount		= ovl_mount,
	.kill_sb	= kill_anon_super,
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
