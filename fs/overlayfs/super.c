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
		 "Default to on or off for the iyesdes index feature");

static bool ovl_nfs_export_def = IS_ENABLED(CONFIG_OVERLAY_FS_NFS_EXPORT);
module_param_named(nfs_export, ovl_nfs_export_def, bool, 0644);
MODULE_PARM_DESC(nfs_export,
		 "Default to on or off for the NFS export feature");

static bool ovl_xiyes_auto_def = IS_ENABLED(CONFIG_OVERLAY_FS_XINO_AUTO);
module_param_named(xiyes_auto, ovl_xiyes_auto_def, bool, 0644);
MODULE_PARM_DESC(xiyes_auto,
		 "Auto enable xiyes feature");

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
				 const struct iyesde *iyesde)
{
	struct dentry *real;

	/* It's an overlay file */
	if (iyesde && d_iyesde(dentry) == iyesde)
		return dentry;

	if (!d_is_reg(dentry)) {
		if (!iyesde || iyesde == d_iyesde(dentry))
			return dentry;
		goto bug;
	}

	real = ovl_dentry_upper(dentry);
	if (real && (iyesde == d_iyesde(real)))
		return real;

	if (real && !iyesde && ovl_has_upperdata(d_iyesde(dentry)))
		return real;

	real = ovl_dentry_lowerdata(dentry);
	if (!real)
		goto bug;

	/* Handle recursion */
	real = d_real(real, iyesde);

	if (!iyesde || iyesde == d_iyesde(real))
		return real;
bug:
	WARN(1, "ovl_d_real(%pd4, %s:%lu): real dentry yest found\n", dentry,
	     iyesde ? iyesde->i_sb->s_id : "NULL", iyesde ? iyesde->i_iyes : 0);
	return dentry;
}

static int ovl_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	unsigned int i;
	int ret = 1;

	for (i = 0; i < oe->numlower; i++) {
		struct dentry *d = oe->lowerstack[i].dentry;

		if (d->d_flags & DCACHE_OP_REVALIDATE) {
			ret = d->d_op->d_revalidate(d, flags);
			if (ret < 0)
				return ret;
			if (!ret) {
				if (!(flags & LOOKUP_RCU))
					d_invalidate(d);
				return -ESTALE;
			}
		}
	}
	return 1;
}

static int ovl_dentry_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	unsigned int i;
	int ret = 1;

	for (i = 0; i < oe->numlower; i++) {
		struct dentry *d = oe->lowerstack[i].dentry;

		if (d->d_flags & DCACHE_OP_WEAK_REVALIDATE) {
			ret = d->d_op->d_weak_revalidate(d, flags);
			if (ret <= 0)
				break;
		}
	}
	return ret;
}

static const struct dentry_operations ovl_dentry_operations = {
	.d_release = ovl_dentry_release,
	.d_real = ovl_d_real,
};

static const struct dentry_operations ovl_reval_dentry_operations = {
	.d_release = ovl_dentry_release,
	.d_real = ovl_d_real,
	.d_revalidate = ovl_dentry_revalidate,
	.d_weak_revalidate = ovl_dentry_weak_revalidate,
};

static struct kmem_cache *ovl_iyesde_cachep;

static struct iyesde *ovl_alloc_iyesde(struct super_block *sb)
{
	struct ovl_iyesde *oi = kmem_cache_alloc(ovl_iyesde_cachep, GFP_KERNEL);

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

	return &oi->vfs_iyesde;
}

static void ovl_free_iyesde(struct iyesde *iyesde)
{
	struct ovl_iyesde *oi = OVL_I(iyesde);

	kfree(oi->redirect);
	mutex_destroy(&oi->lock);
	kmem_cache_free(ovl_iyesde_cachep, oi);
}

static void ovl_destroy_iyesde(struct iyesde *iyesde)
{
	struct ovl_iyesde *oi = OVL_I(iyesde);

	dput(oi->__upperdentry);
	iput(oi->lower);
	if (S_ISDIR(iyesde->i_mode))
		ovl_dir_cache_free(iyesde);
	else
		iput(oi->lowerdata);
}

static void ovl_free_fs(struct ovl_fs *ofs)
{
	unsigned i;

	iput(ofs->workbasedir_trap);
	iput(ofs->indexdir_trap);
	iput(ofs->workdir_trap);
	iput(ofs->upperdir_trap);
	dput(ofs->indexdir);
	dput(ofs->workdir);
	if (ofs->workdir_locked)
		ovl_inuse_unlock(ofs->workbasedir);
	dput(ofs->workbasedir);
	if (ofs->upperdir_locked)
		ovl_inuse_unlock(ofs->upper_mnt->mnt_root);
	mntput(ofs->upper_mnt);
	for (i = 0; i < ofs->numlower; i++) {
		iput(ofs->lower_layers[i].trap);
		mntput(ofs->lower_layers[i].mnt);
	}
	for (i = 0; i < ofs->numlowerfs; i++)
		free_ayesn_bdev(ofs->lower_fs[i].pseudo_dev);
	kfree(ofs->lower_layers);
	kfree(ofs->lower_fs);

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

/* Sync real dirty iyesdes in upper filesystem (if it exists) */
static int ovl_sync_fs(struct super_block *sb, int wait)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct super_block *upper_sb;
	int ret;

	if (!ofs->upper_mnt)
		return 0;

	/*
	 * If this is a sync(2) call or an emergency sync, all the super blocks
	 * will be iterated, including upper_sb, so yes need to do anything.
	 *
	 * If this is a syncfs(2) call, then we do need to call
	 * sync_filesystem() on upper_sb, but eyesugh if we do it when being
	 * called with wait == 1.
	 */
	if (!wait)
		return 0;

	upper_sb = ofs->upper_mnt->mnt_sb;

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
	return (!ofs->upper_mnt || !ofs->workdir);
}

static const char *ovl_redirect_mode_def(void)
{
	return ovl_redirect_dir_def ? "on" : "off";
}

enum {
	OVL_XINO_OFF,
	OVL_XINO_AUTO,
	OVL_XINO_ON,
};

static const char * const ovl_xiyes_str[] = {
	"off",
	"auto",
	"on",
};

static inline int ovl_xiyes_def(void)
{
	return ovl_xiyes_auto_def ? OVL_XINO_AUTO : OVL_XINO_OFF;
}

/**
 * ovl_show_options
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does yest fail.
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
	if (ofs->config.nfs_export != ovl_nfs_export_def)
		seq_printf(m, ",nfs_export=%s", ofs->config.nfs_export ?
						"on" : "off");
	if (ofs->config.xiyes != ovl_xiyes_def())
		seq_printf(m, ",xiyes=%s", ovl_xiyes_str[ofs->config.xiyes]);
	if (ofs->config.metacopy != ovl_metacopy_def)
		seq_printf(m, ",metacopy=%s",
			   ofs->config.metacopy ? "on" : "off");
	return 0;
}

static int ovl_remount(struct super_block *sb, int *flags, char *data)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	if (!(*flags & SB_RDONLY) && ovl_force_readonly(ofs))
		return -EROFS;

	return 0;
}

static const struct super_operations ovl_super_operations = {
	.alloc_iyesde	= ovl_alloc_iyesde,
	.free_iyesde	= ovl_free_iyesde,
	.destroy_iyesde	= ovl_destroy_iyesde,
	.drop_iyesde	= generic_delete_iyesde,
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
	OPT_NFS_EXPORT_ON,
	OPT_NFS_EXPORT_OFF,
	OPT_XINO_ON,
	OPT_XINO_OFF,
	OPT_XINO_AUTO,
	OPT_METACOPY_ON,
	OPT_METACOPY_OFF,
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
	{OPT_NFS_EXPORT_ON,		"nfs_export=on"},
	{OPT_NFS_EXPORT_OFF,		"nfs_export=off"},
	{OPT_XINO_ON,			"xiyes=on"},
	{OPT_XINO_OFF,			"xiyes=off"},
	{OPT_XINO_AUTO,			"xiyes=auto"},
	{OPT_METACOPY_ON,		"metacopy=on"},
	{OPT_METACOPY_OFF,		"metacopy=off"},
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
		 * Does yest make sense to have redirect creation without
		 * redirect following.
		 */
		config->redirect_follow = true;
	} else if (strcmp(mode, "follow") == 0) {
		config->redirect_follow = true;
	} else if (strcmp(mode, "off") == 0) {
		if (ovl_redirect_always_follow)
			config->redirect_follow = true;
	} else if (strcmp(mode, "yesfollow") != 0) {
		pr_err("overlayfs: bad mount option \"redirect_dir=%s\"\n",
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
			break;

		case OPT_INDEX_OFF:
			config->index = false;
			break;

		case OPT_NFS_EXPORT_ON:
			config->nfs_export = true;
			break;

		case OPT_NFS_EXPORT_OFF:
			config->nfs_export = false;
			break;

		case OPT_XINO_ON:
			config->xiyes = OVL_XINO_ON;
			break;

		case OPT_XINO_OFF:
			config->xiyes = OVL_XINO_OFF;
			break;

		case OPT_XINO_AUTO:
			config->xiyes = OVL_XINO_AUTO;
			break;

		case OPT_METACOPY_ON:
			config->metacopy = true;
			metacopy_opt = true;
			break;

		case OPT_METACOPY_OFF:
			config->metacopy = false;
			break;

		default:
			pr_err("overlayfs: unrecognized mount option \"%s\" or missing value\n", p);
			return -EINVAL;
		}
	}

	/* Workdir is useless in yesn-upper mount */
	if (!config->upperdir && config->workdir) {
		pr_info("overlayfs: option \"workdir=%s\" is useless in a yesn-upper mount, igyesre\n",
			config->workdir);
		kfree(config->workdir);
		config->workdir = NULL;
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
			pr_err("overlayfs: conflicting options: metacopy=on,redirect_dir=%s\n",
			       config->redirect_mode);
			return -EINVAL;
		}
		if (redirect_opt) {
			/*
			 * There was an explicit redirect_dir=... that resulted
			 * in this conflict.
			 */
			pr_info("overlayfs: disabling metacopy due to redirect_dir=%s\n",
				config->redirect_mode);
			config->metacopy = false;
		} else {
			/* Automatically enable redirect otherwise. */
			config->redirect_follow = config->redirect_dir = true;
		}
	}

	return 0;
}

#define OVL_WORKDIR_NAME "work"
#define OVL_INDEXDIR_NAME "index"

static struct dentry *ovl_workdir_create(struct ovl_fs *ofs,
					 const char *name, bool persist)
{
	struct iyesde *dir =  ofs->workbasedir->d_iyesde;
	struct vfsmount *mnt = ofs->upper_mnt;
	struct dentry *work;
	int err;
	bool retried = false;
	bool locked = false;

	iyesde_lock_nested(dir, I_MUTEX_PARENT);
	locked = true;

retry:
	work = lookup_one_len(name, ofs->workbasedir, strlen(name));

	if (!IS_ERR(work)) {
		struct iattr attr = {
			.ia_valid = ATTR_MODE,
			.ia_mode = S_IFDIR | 0,
		};

		if (work->d_iyesde) {
			err = -EEXIST;
			if (retried)
				goto out_dput;

			if (persist)
				goto out_unlock;

			retried = true;
			ovl_workdir_cleanup(dir, mnt, work, 0);
			dput(work);
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
		 * b) -ENODATA (there was yes POSIX ACL xattr)
		 * c) -EOPNOTSUPP (POSIX ACL xattrs are yest supported)
		 *
		 * There are various other error values that could effectively
		 * mean that the xattr doesn't exist (e.g. -ERANGE is returned
		 * if the xattr name is too long), but the set of filesystems
		 * allowed as upper are limited to "yesrmal" ones, where checking
		 * for the above two errors is sufficient.
		 */
		err = vfs_removexattr(work, XATTR_NAME_POSIX_ACL_DEFAULT);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		err = vfs_removexattr(work, XATTR_NAME_POSIX_ACL_ACCESS);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		/* Clear any inherited mode bits */
		iyesde_lock(work->d_iyesde);
		err = yestify_change(work, &attr, NULL);
		iyesde_unlock(work->d_iyesde);
		if (err)
			goto out_dput;
	} else {
		err = PTR_ERR(work);
		goto out_err;
	}
out_unlock:
	if (locked)
		iyesde_unlock(dir);

	return work;

out_dput:
	dput(work);
out_err:
	pr_warn("overlayfs: failed to create directory %s/%s (erryes: %i); mounting read-only\n",
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

static int ovl_mount_dir_yesesc(const char *name, struct path *path)
{
	int err = -EINVAL;

	if (!*name) {
		pr_err("overlayfs: empty lowerdir\n");
		goto out;
	}
	err = kern_path(name, LOOKUP_FOLLOW, path);
	if (err) {
		pr_err("overlayfs: failed to resolve '%s': %i\n", name, err);
		goto out;
	}
	err = -EINVAL;
	if (ovl_dentry_weird(path->dentry)) {
		pr_err("overlayfs: filesystem on '%s' yest supported\n", name);
		goto out_put;
	}
	if (!d_is_dir(path->dentry)) {
		pr_err("overlayfs: '%s' yest a directory\n", name);
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
		err = ovl_mount_dir_yesesc(tmp, path);

		if (!err)
			if (ovl_dentry_remote(path->dentry)) {
				pr_err("overlayfs: filesystem on '%s' yest supported as upperdir\n",
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
		pr_err("overlayfs: statfs failed on '%s'\n", name);
	else
		ofs->namelen = max(ofs->namelen, statfs.f_namelen);

	return err;
}

static int ovl_lower_dir(const char *name, struct path *path,
			 struct ovl_fs *ofs, int *stack_depth, bool *remote)
{
	int fh_type;
	int err;

	err = ovl_mount_dir_yesesc(name, path);
	if (err)
		goto out;

	err = ovl_check_namelen(path, ofs, name);
	if (err)
		goto out_put;

	*stack_depth = max(*stack_depth, path->mnt->mnt_sb->s_stack_depth);

	if (ovl_dentry_remote(path->dentry))
		*remote = true;

	/*
	 * The iyesdes index feature and NFS export need to encode and decode
	 * file handles, so they require that all layers support them.
	 */
	fh_type = ovl_can_decode_fh(path->dentry->d_sb);
	if ((ofs->config.nfs_export ||
	     (ofs->config.index && ofs->config.upperdir)) && !fh_type) {
		ofs->config.index = false;
		ofs->config.nfs_export = false;
		pr_warn("overlayfs: fs on '%s' does yest support file handles, falling back to index=off,nfs_export=off.\n",
			name);
	}

	/* Check if lower fs has 32bit iyesde numbers */
	if (fh_type != FILEID_INO32_GEN)
		ofs->xiyes_bits = 0;

	return 0;

out_put:
	path_put_init(path);
out:
	return err;
}

/* Workdir should yest be subdir of upperdir and vice versa */
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
			struct dentry *dentry, struct iyesde *iyesde,
			const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, iyesde, handler->name, buffer, size);
}

static int __maybe_unused
ovl_posix_acl_xattr_set(const struct xattr_handler *handler,
			struct dentry *dentry, struct iyesde *iyesde,
			const char *name, const void *value,
			size_t size, int flags)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct iyesde *realiyesde = ovl_iyesde_real(iyesde);
	struct posix_acl *acl = NULL;
	int err;

	/* Check that everything is OK before copy-up */
	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	err = -EOPNOTSUPP;
	if (!IS_POSIXACL(d_iyesde(workdir)))
		goto out_acl_release;
	if (!realiyesde->i_op->set_acl)
		goto out_acl_release;
	if (handler->flags == ACL_TYPE_DEFAULT && !S_ISDIR(iyesde->i_mode)) {
		err = acl ? -EACCES : 0;
		goto out_acl_release;
	}
	err = -EPERM;
	if (!iyesde_owner_or_capable(iyesde))
		goto out_acl_release;

	posix_acl_release(acl);

	/*
	 * Check if sgid bit needs to be cleared (actual setacl operation will
	 * be done with mounter's capabilities and so that won't do it for us).
	 */
	if (unlikely(iyesde->i_mode & S_ISGID) &&
	    handler->flags == ACL_TYPE_ACCESS &&
	    !in_group_p(iyesde->i_gid) &&
	    !capable_wrt_iyesde_uidgid(iyesde, CAP_FSETID)) {
		struct iattr iattr = { .ia_valid = ATTR_KILL_SGID };

		err = ovl_setattr(dentry, &iattr);
		if (err)
			return err;
	}

	err = ovl_xattr_set(dentry, iyesde, handler->name, value, size, flags);
	if (!err)
		ovl_copyattr(ovl_iyesde_real(iyesde), iyesde);

	return err;

out_acl_release:
	posix_acl_release(acl);
	return err;
}

static int ovl_own_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct iyesde *iyesde,
			     const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static int ovl_own_xattr_set(const struct xattr_handler *handler,
			     struct dentry *dentry, struct iyesde *iyesde,
			     const char *name, const void *value,
			     size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static int ovl_other_xattr_get(const struct xattr_handler *handler,
			       struct dentry *dentry, struct iyesde *iyesde,
			       const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, iyesde, name, buffer, size);
}

static int ovl_other_xattr_set(const struct xattr_handler *handler,
			       struct dentry *dentry, struct iyesde *iyesde,
			       const char *name, const void *value,
			       size_t size, int flags)
{
	return ovl_xattr_set(dentry, iyesde, name, value, size, flags);
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

static const struct xattr_handler ovl_own_xattr_handler = {
	.prefix	= OVL_XATTR_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_other_xattr_handler = {
	.prefix	= "", /* catch all */
	.get = ovl_other_xattr_get,
	.set = ovl_other_xattr_set,
};

static const struct xattr_handler *ovl_xattr_handlers[] = {
#ifdef CONFIG_FS_POSIX_ACL
	&ovl_posix_acl_access_xattr_handler,
	&ovl_posix_acl_default_xattr_handler,
#endif
	&ovl_own_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static int ovl_setup_trap(struct super_block *sb, struct dentry *dir,
			  struct iyesde **ptrap, const char *name)
{
	struct iyesde *trap;
	int err;

	trap = ovl_get_trap_iyesde(sb, dir);
	err = PTR_ERR_OR_ZERO(trap);
	if (err) {
		if (err == -ELOOP)
			pr_err("overlayfs: conflicting %s path\n", name);
		return err;
	}

	*ptrap = trap;
	return 0;
}

/*
 * Determine how we treat concurrent use of upperdir/workdir based on the
 * index feature. This is papering over mount leaks of container runtimes,
 * for example, an old overlay mount is leaked and yesw its upperdir is
 * attempted to be used as a lower layer in a new overlay mount.
 */
static int ovl_report_in_use(struct ovl_fs *ofs, const char *name)
{
	if (ofs->config.index) {
		pr_err("overlayfs: %s is in-use as upperdir/workdir of ayesther mount, mount with '-o index=off' to override exclusive upperdir protection.\n",
		       name);
		return -EBUSY;
	} else {
		pr_warn("overlayfs: %s is in-use as upperdir/workdir of ayesther mount, accessing files from both mounts will result in undefined behavior.\n",
			name);
		return 0;
	}
}

static int ovl_get_upper(struct super_block *sb, struct ovl_fs *ofs,
			 struct path *upperpath)
{
	struct vfsmount *upper_mnt;
	int err;

	err = ovl_mount_dir(ofs->config.upperdir, upperpath);
	if (err)
		goto out;

	/* Upper fs should yest be r/o */
	if (sb_rdonly(upperpath->mnt->mnt_sb)) {
		pr_err("overlayfs: upper fs is r/o, try multi-lower layers mount\n");
		err = -EINVAL;
		goto out;
	}

	err = ovl_check_namelen(upperpath, ofs, ofs->config.upperdir);
	if (err)
		goto out;

	err = ovl_setup_trap(sb, upperpath->dentry, &ofs->upperdir_trap,
			     "upperdir");
	if (err)
		goto out;

	upper_mnt = clone_private_mount(upperpath);
	err = PTR_ERR(upper_mnt);
	if (IS_ERR(upper_mnt)) {
		pr_err("overlayfs: failed to clone upperpath\n");
		goto out;
	}

	/* Don't inherit atime flags */
	upper_mnt->mnt_flags &= ~(MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME);
	ofs->upper_mnt = upper_mnt;

	if (ovl_inuse_trylock(ofs->upper_mnt->mnt_root)) {
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

static int ovl_make_workdir(struct super_block *sb, struct ovl_fs *ofs,
			    struct path *workpath)
{
	struct vfsmount *mnt = ofs->upper_mnt;
	struct dentry *temp;
	int fh_type;
	int err;

	err = mnt_want_write(mnt);
	if (err)
		return err;

	ofs->workdir = ovl_workdir_create(ofs, OVL_WORKDIR_NAME, false);
	if (!ofs->workdir)
		goto out;

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

	/*
	 * We allowed this configuration and don't want to break users over
	 * kernel upgrade. So warn instead of erroring out.
	 */
	if (!err)
		pr_warn("overlayfs: upper fs needs to support d_type.\n");

	/* Check if upper/work fs supports O_TMPFILE */
	temp = ovl_do_tmpfile(ofs->workdir, S_IFREG | 0);
	ofs->tmpfile = !IS_ERR(temp);
	if (ofs->tmpfile)
		dput(temp);
	else
		pr_warn("overlayfs: upper fs does yest support tmpfile.\n");

	/*
	 * Check if upper/work fs supports trusted.overlay.* xattr
	 */
	err = ovl_do_setxattr(ofs->workdir, OVL_XATTR_OPAQUE, "0", 1, 0);
	if (err) {
		ofs->yesxattr = true;
		ofs->config.index = false;
		ofs->config.metacopy = false;
		pr_warn("overlayfs: upper fs does yest support xattr, falling back to index=off and metacopy=off.\n");
		err = 0;
	} else {
		vfs_removexattr(ofs->workdir, OVL_XATTR_OPAQUE);
	}

	/* Check if upper/work fs supports file handles */
	fh_type = ovl_can_decode_fh(ofs->workdir->d_sb);
	if (ofs->config.index && !fh_type) {
		ofs->config.index = false;
		pr_warn("overlayfs: upper fs does yest support file handles, falling back to index=off.\n");
	}

	/* Check if upper fs has 32bit iyesde numbers */
	if (fh_type != FILEID_INO32_GEN)
		ofs->xiyes_bits = 0;

	/* NFS export of r/w mount depends on index */
	if (ofs->config.nfs_export && !ofs->config.index) {
		pr_warn("overlayfs: NFS export requires \"index=on\", falling back to nfs_export=off.\n");
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
		pr_err("overlayfs: workdir and upperdir must reside under the same mount\n");
		goto out;
	}
	if (!ovl_workdir_ok(workpath.dentry, upperpath->dentry)) {
		pr_err("overlayfs: workdir and upperdir must be separate subtrees\n");
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
	struct vfsmount *mnt = ofs->upper_mnt;
	int err;

	err = mnt_want_write(mnt);
	if (err)
		return err;

	/* Verify lower root is upper root origin */
	err = ovl_verify_origin(upperpath->dentry, oe->lowerstack[0].dentry,
				true);
	if (err) {
		pr_err("overlayfs: failed to verify upper root origin\n");
		goto out;
	}

	ofs->indexdir = ovl_workdir_create(ofs, OVL_INDEXDIR_NAME, true);
	if (ofs->indexdir) {
		err = ovl_setup_trap(sb, ofs->indexdir, &ofs->indexdir_trap,
				     "indexdir");
		if (err)
			goto out;

		/*
		 * Verify upper root is exclusively associated with index dir.
		 * Older kernels stored upper fh in "trusted.overlay.origin"
		 * xattr. If that xattr exists, verify that it is a match to
		 * upper dir file handle. In any case, verify or set xattr
		 * "trusted.overlay.upper" to indicate that index may have
		 * directory entries.
		 */
		if (ovl_check_origin_xattr(ofs->indexdir)) {
			err = ovl_verify_set_fh(ofs->indexdir, OVL_XATTR_ORIGIN,
						upperpath->dentry, true, false);
			if (err)
				pr_err("overlayfs: failed to verify index dir 'origin' xattr\n");
		}
		err = ovl_verify_upper(ofs->indexdir, upperpath->dentry, true);
		if (err)
			pr_err("overlayfs: failed to verify index dir 'upper' xattr\n");

		/* Cleanup bad/stale/orphan index entries */
		if (!err)
			err = ovl_indexdir_cleanup(ofs);
	}
	if (err || !ofs->indexdir)
		pr_warn("overlayfs: try deleting index dir or mounting with '-o index=off' to disable iyesdes index.\n");

out:
	mnt_drop_write(mnt);
	return err;
}

static bool ovl_lower_uuid_ok(struct ovl_fs *ofs, const uuid_t *uuid)
{
	unsigned int i;

	if (!ofs->config.nfs_export && !ofs->upper_mnt)
		return true;

	for (i = 0; i < ofs->numlowerfs; i++) {
		/*
		 * We use uuid to associate an overlay lower file handle with a
		 * lower layer, so we can accept lower fs with null uuid as long
		 * as all lower layers with null uuid are on the same fs.
		 * if we detect multiple lower fs with the same uuid, we
		 * disable lower file handle decoding on all of them.
		 */
		if (uuid_equal(&ofs->lower_fs[i].sb->s_uuid, uuid)) {
			ofs->lower_fs[i].bad_uuid = true;
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

	/* fsid 0 is reserved for upper fs even with yesn upper overlay */
	if (ofs->upper_mnt && ofs->upper_mnt->mnt_sb == sb)
		return 0;

	for (i = 0; i < ofs->numlowerfs; i++) {
		if (ofs->lower_fs[i].sb == sb)
			return i + 1;
	}

	if (!ovl_lower_uuid_ok(ofs, &sb->s_uuid)) {
		bad_uuid = true;
		if (ofs->config.index || ofs->config.nfs_export) {
			ofs->config.index = false;
			ofs->config.nfs_export = false;
			pr_warn("overlayfs: %s uuid detected in lower fs '%pd2', falling back to index=off,nfs_export=off.\n",
				uuid_is_null(&sb->s_uuid) ? "null" :
							    "conflicting",
				path->dentry);
		}
	}

	err = get_ayesn_bdev(&dev);
	if (err) {
		pr_err("overlayfs: failed to get ayesnymous bdev for lowerpath\n");
		return err;
	}

	ofs->lower_fs[ofs->numlowerfs].sb = sb;
	ofs->lower_fs[ofs->numlowerfs].pseudo_dev = dev;
	ofs->lower_fs[ofs->numlowerfs].bad_uuid = bad_uuid;
	ofs->numlowerfs++;

	return ofs->numlowerfs;
}

static int ovl_get_lower_layers(struct super_block *sb, struct ovl_fs *ofs,
				struct path *stack, unsigned int numlower)
{
	int err;
	unsigned int i;

	err = -ENOMEM;
	ofs->lower_layers = kcalloc(numlower, sizeof(struct ovl_layer),
				    GFP_KERNEL);
	if (ofs->lower_layers == NULL)
		goto out;

	ofs->lower_fs = kcalloc(numlower, sizeof(struct ovl_sb),
				GFP_KERNEL);
	if (ofs->lower_fs == NULL)
		goto out;

	for (i = 0; i < numlower; i++) {
		struct vfsmount *mnt;
		struct iyesde *trap;
		int fsid;

		err = fsid = ovl_get_fsid(ofs, &stack[i]);
		if (err < 0)
			goto out;

		err = ovl_setup_trap(sb, stack[i].dentry, &trap, "lowerdir");
		if (err)
			goto out;

		if (ovl_is_inuse(stack[i].dentry)) {
			err = ovl_report_in_use(ofs, "lowerdir");
			if (err)
				goto out;
		}

		mnt = clone_private_mount(&stack[i]);
		err = PTR_ERR(mnt);
		if (IS_ERR(mnt)) {
			pr_err("overlayfs: failed to clone lowerpath\n");
			iput(trap);
			goto out;
		}

		/*
		 * Make lower layers R/O.  That way fchmod/fchown on lower file
		 * will fail instead of modifying lower fs.
		 */
		mnt->mnt_flags |= MNT_READONLY | MNT_NOATIME;

		ofs->lower_layers[ofs->numlower].trap = trap;
		ofs->lower_layers[ofs->numlower].mnt = mnt;
		ofs->lower_layers[ofs->numlower].idx = i + 1;
		ofs->lower_layers[ofs->numlower].fsid = fsid;
		if (fsid) {
			ofs->lower_layers[ofs->numlower].fs =
				&ofs->lower_fs[fsid - 1];
		}
		ofs->numlower++;
	}

	/*
	 * When all layers on same fs, overlay can use real iyesde numbers.
	 * With mount option "xiyes=on", mounter declares that there are eyesugh
	 * free high bits in underlying fs to hold the unique fsid.
	 * If overlayfs does encounter underlying iyesdes using the high xiyes
	 * bits reserved for fsid, it emits a warning and uses the original
	 * iyesde number.
	 */
	if (!ofs->numlowerfs || (ofs->numlowerfs == 1 && !ofs->upper_mnt)) {
		ofs->xiyes_bits = 0;
		ofs->config.xiyes = OVL_XINO_OFF;
	} else if (ofs->config.xiyes == OVL_XINO_ON && !ofs->xiyes_bits) {
		/*
		 * This is a roundup of number of bits needed for numlowerfs+1
		 * (i.e. ilog2(numlowerfs+1 - 1) + 1). fsid 0 is reserved for
		 * upper fs even with yesn upper overlay.
		 */
		BUILD_BUG_ON(ilog2(OVL_MAX_STACK) > 31);
		ofs->xiyes_bits = ilog2(ofs->numlowerfs) + 1;
	}

	if (ofs->xiyes_bits) {
		pr_info("overlayfs: \"xiyes\" feature enabled using %d upper iyesde bits.\n",
			ofs->xiyes_bits);
	}

	err = 0;
out:
	return err;
}

static struct ovl_entry *ovl_get_lowerstack(struct super_block *sb,
					    struct ovl_fs *ofs)
{
	int err;
	char *lowertmp, *lower;
	struct path *stack = NULL;
	unsigned int stacklen, numlower = 0, i;
	bool remote = false;
	struct ovl_entry *oe;

	err = -ENOMEM;
	lowertmp = kstrdup(ofs->config.lowerdir, GFP_KERNEL);
	if (!lowertmp)
		goto out_err;

	err = -EINVAL;
	stacklen = ovl_split_lowerdirs(lowertmp);
	if (stacklen > OVL_MAX_STACK) {
		pr_err("overlayfs: too many lower directories, limit is %d\n",
		       OVL_MAX_STACK);
		goto out_err;
	} else if (!ofs->config.upperdir && stacklen == 1) {
		pr_err("overlayfs: at least 2 lowerdir are needed while upperdir yesnexistent\n");
		goto out_err;
	} else if (!ofs->config.upperdir && ofs->config.nfs_export &&
		   ofs->config.redirect_follow) {
		pr_warn("overlayfs: NFS export requires \"redirect_dir=yesfollow\" on yesn-upper mount, falling back to nfs_export=off.\n");
		ofs->config.nfs_export = false;
	}

	err = -ENOMEM;
	stack = kcalloc(stacklen, sizeof(struct path), GFP_KERNEL);
	if (!stack)
		goto out_err;

	err = -EINVAL;
	lower = lowertmp;
	for (numlower = 0; numlower < stacklen; numlower++) {
		err = ovl_lower_dir(lower, &stack[numlower], ofs,
				    &sb->s_stack_depth, &remote);
		if (err)
			goto out_err;

		lower = strchr(lower, '\0') + 1;
	}

	err = -EINVAL;
	sb->s_stack_depth++;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("overlayfs: maximum fs stacking depth exceeded\n");
		goto out_err;
	}

	err = ovl_get_lower_layers(sb, ofs, stack, numlower);
	if (err)
		goto out_err;

	err = -ENOMEM;
	oe = ovl_alloc_entry(numlower);
	if (!oe)
		goto out_err;

	for (i = 0; i < numlower; i++) {
		oe->lowerstack[i].dentry = dget(stack[i].dentry);
		oe->lowerstack[i].layer = &ofs->lower_layers[i];
	}

	if (remote)
		sb->s_d_op = &ovl_reval_dentry_operations;
	else
		sb->s_d_op = &ovl_dentry_operations;

out:
	for (i = 0; i < numlower; i++)
		path_put(&stack[i]);
	kfree(stack);
	kfree(lowertmp);

	return oe;

out_err:
	oe = ERR_PTR(err);
	goto out;
}

/*
 * Check if this layer root is a descendant of:
 * - ayesther layer of this overlayfs instance
 * - upper/work dir of any overlayfs instance
 */
static int ovl_check_layer(struct super_block *sb, struct ovl_fs *ofs,
			   struct dentry *dentry, const char *name)
{
	struct dentry *next = dentry, *parent;
	int err = 0;

	if (!dentry)
		return 0;

	parent = dget_parent(next);

	/* Walk back ancestors to root (inclusive) looking for traps */
	while (!err && parent != next) {
		if (ovl_lookup_trap_iyesde(sb, parent)) {
			err = -ELOOP;
			pr_err("overlayfs: overlapping %s path\n", name);
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

	if (ofs->upper_mnt) {
		err = ovl_check_layer(sb, ofs, ofs->upper_mnt->mnt_root,
				      "upperdir");
		if (err)
			return err;

		/*
		 * Checking workbasedir avoids hitting ovl_is_inuse(parent) of
		 * this instance and covers overlapping work and index dirs,
		 * unless work or index dir have been moved since created inside
		 * workbasedir.  In that case, we already have their traps in
		 * iyesde cache and we will catch that case on lookup.
		 */
		err = ovl_check_layer(sb, ofs, ofs->workbasedir, "workdir");
		if (err)
			return err;
	}

	for (i = 0; i < ofs->numlower; i++) {
		err = ovl_check_layer(sb, ofs,
				      ofs->lower_layers[i].mnt->mnt_root,
				      "lowerdir");
		if (err)
			return err;
	}

	return 0;
}

static int ovl_fill_super(struct super_block *sb, void *data, int silent)
{
	struct path upperpath = { };
	struct dentry *root_dentry;
	struct ovl_entry *oe;
	struct ovl_fs *ofs;
	struct cred *cred;
	int err;

	err = -ENOMEM;
	ofs = kzalloc(sizeof(struct ovl_fs), GFP_KERNEL);
	if (!ofs)
		goto out;

	ofs->creator_cred = cred = prepare_creds();
	if (!cred)
		goto out_err;

	ofs->config.index = ovl_index_def;
	ofs->config.nfs_export = ovl_nfs_export_def;
	ofs->config.xiyes = ovl_xiyes_def();
	ofs->config.metacopy = ovl_metacopy_def;
	err = ovl_parse_opt((char *) data, &ofs->config);
	if (err)
		goto out_err;

	err = -EINVAL;
	if (!ofs->config.lowerdir) {
		if (!silent)
			pr_err("overlayfs: missing 'lowerdir'\n");
		goto out_err;
	}

	sb->s_stack_depth = 0;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	/* Assume underlaying fs uses 32bit iyesdes unless proven otherwise */
	if (ofs->config.xiyes != OVL_XINO_OFF)
		ofs->xiyes_bits = BITS_PER_LONG - 32;

	/* alloc/destroy_iyesde needed for setting up traps in iyesde cache */
	sb->s_op = &ovl_super_operations;

	if (ofs->config.upperdir) {
		if (!ofs->config.workdir) {
			pr_err("overlayfs: missing 'workdir'\n");
			goto out_err;
		}

		err = ovl_get_upper(sb, ofs, &upperpath);
		if (err)
			goto out_err;

		err = ovl_get_workdir(sb, ofs, &upperpath);
		if (err)
			goto out_err;

		if (!ofs->workdir)
			sb->s_flags |= SB_RDONLY;

		sb->s_stack_depth = ofs->upper_mnt->mnt_sb->s_stack_depth;
		sb->s_time_gran = ofs->upper_mnt->mnt_sb->s_time_gran;

	}
	oe = ovl_get_lowerstack(sb, ofs);
	err = PTR_ERR(oe);
	if (IS_ERR(oe))
		goto out_err;

	/* If the upper fs is yesnexistent, we mark overlayfs r/o too */
	if (!ofs->upper_mnt)
		sb->s_flags |= SB_RDONLY;

	if (!(ovl_force_readonly(ofs)) && ofs->config.index) {
		err = ovl_get_indexdir(sb, ofs, oe, &upperpath);
		if (err)
			goto out_free_oe;

		/* Force r/o mount with yes index dir */
		if (!ofs->indexdir) {
			dput(ofs->workdir);
			ofs->workdir = NULL;
			sb->s_flags |= SB_RDONLY;
		}

	}

	err = ovl_check_overlapping_layers(sb, ofs);
	if (err)
		goto out_free_oe;

	/* Show index=off in /proc/mounts for forced r/o mount */
	if (!ofs->indexdir) {
		ofs->config.index = false;
		if (ofs->upper_mnt && ofs->config.nfs_export) {
			pr_warn("overlayfs: NFS export requires an index dir, falling back to nfs_export=off.\n");
			ofs->config.nfs_export = false;
		}
	}

	if (ofs->config.metacopy && ofs->config.nfs_export) {
		pr_warn("overlayfs: NFS export is yest supported with metadata only copy up, falling back to nfs_export=off.\n");
		ofs->config.nfs_export = false;
	}

	if (ofs->config.nfs_export)
		sb->s_export_op = &ovl_export_operations;

	/* Never override disk quota limits or use reserved space */
	cap_lower(cred->cap_effective, CAP_SYS_RESOURCE);

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_xattr = ovl_xattr_handlers;
	sb->s_fs_info = ofs;
	sb->s_flags |= SB_POSIXACL;

	err = -ENOMEM;
	root_dentry = d_make_root(ovl_new_iyesde(sb, S_IFDIR, 0));
	if (!root_dentry)
		goto out_free_oe;

	root_dentry->d_fsdata = oe;

	mntput(upperpath.mnt);
	if (upperpath.dentry) {
		ovl_dentry_set_upper_alias(root_dentry);
		if (ovl_is_impuredir(upperpath.dentry))
			ovl_set_flag(OVL_IMPURE, d_iyesde(root_dentry));
	}

	/* Root is always merge -> can have whiteouts */
	ovl_set_flag(OVL_WHITEOUTS, d_iyesde(root_dentry));
	ovl_dentry_set_flag(OVL_E_CONNECTED, root_dentry);
	ovl_set_upperdata(d_iyesde(root_dentry));
	ovl_iyesde_init(d_iyesde(root_dentry), upperpath.dentry,
		       ovl_dentry_lower(root_dentry), NULL);

	sb->s_root = root_dentry;

	return 0;

out_free_oe:
	ovl_entry_stack_free(oe);
	kfree(oe);
out_err:
	path_put(&upperpath);
	ovl_free_fs(ofs);
out:
	return err;
}

static struct dentry *ovl_mount(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *raw_data)
{
	return mount_yesdev(fs_type, flags, raw_data, ovl_fill_super);
}

static struct file_system_type ovl_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "overlay",
	.mount		= ovl_mount,
	.kill_sb	= kill_ayesn_super,
};
MODULE_ALIAS_FS("overlay");

static void ovl_iyesde_init_once(void *foo)
{
	struct ovl_iyesde *oi = foo;

	iyesde_init_once(&oi->vfs_iyesde);
}

static int __init ovl_init(void)
{
	int err;

	ovl_iyesde_cachep = kmem_cache_create("ovl_iyesde",
					     sizeof(struct ovl_iyesde), 0,
					     (SLAB_RECLAIM_ACCOUNT|
					      SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     ovl_iyesde_init_once);
	if (ovl_iyesde_cachep == NULL)
		return -ENOMEM;

	err = register_filesystem(&ovl_fs_type);
	if (err)
		kmem_cache_destroy(ovl_iyesde_cachep);

	return err;
}

static void __exit ovl_exit(void)
{
	unregister_filesystem(&ovl_fs_type);

	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ovl_iyesde_cachep);

}

module_init(ovl_init);
module_exit(ovl_exit);
