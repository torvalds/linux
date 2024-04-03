// SPDX-License-Identifier: GPL-2.0-only

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/posix_acl_xattr.h>
#include <linux/seq_file.h>
#include <linux/xattr.h>
#include "overlayfs.h"
#include "params.h"

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

static bool ovl_xino_auto_def = IS_ENABLED(CONFIG_OVERLAY_FS_XINO_AUTO);
module_param_named(xino_auto, ovl_xino_auto_def, bool, 0644);
MODULE_PARM_DESC(xino_auto,
		 "Auto enable xino feature");

static bool ovl_index_def = IS_ENABLED(CONFIG_OVERLAY_FS_INDEX);
module_param_named(index, ovl_index_def, bool, 0644);
MODULE_PARM_DESC(index,
		 "Default to on or off for the inodes index feature");

static bool ovl_nfs_export_def = IS_ENABLED(CONFIG_OVERLAY_FS_NFS_EXPORT);
module_param_named(nfs_export, ovl_nfs_export_def, bool, 0644);
MODULE_PARM_DESC(nfs_export,
		 "Default to on or off for the NFS export feature");

static bool ovl_metacopy_def = IS_ENABLED(CONFIG_OVERLAY_FS_METACOPY);
module_param_named(metacopy, ovl_metacopy_def, bool, 0644);
MODULE_PARM_DESC(metacopy,
		 "Default to on or off for the metadata only copy up feature");

enum ovl_opt {
	Opt_lowerdir,
	Opt_lowerdir_add,
	Opt_datadir_add,
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
	Opt_verity,
	Opt_volatile,
};

static const struct constant_table ovl_parameter_bool[] = {
	{ "on",		true  },
	{ "off",	false },
	{}
};

static const struct constant_table ovl_parameter_uuid[] = {
	{ "off",	OVL_UUID_OFF  },
	{ "null",	OVL_UUID_NULL },
	{ "auto",	OVL_UUID_AUTO },
	{ "on",		OVL_UUID_ON   },
	{}
};

static const char *ovl_uuid_mode(struct ovl_config *config)
{
	return ovl_parameter_uuid[config->uuid].name;
}

static int ovl_uuid_def(void)
{
	return OVL_UUID_AUTO;
}

static const struct constant_table ovl_parameter_xino[] = {
	{ "off",	OVL_XINO_OFF  },
	{ "auto",	OVL_XINO_AUTO },
	{ "on",		OVL_XINO_ON   },
	{}
};

const char *ovl_xino_mode(struct ovl_config *config)
{
	return ovl_parameter_xino[config->xino].name;
}

static int ovl_xino_def(void)
{
	return ovl_xino_auto_def ? OVL_XINO_AUTO : OVL_XINO_OFF;
}

const struct constant_table ovl_parameter_redirect_dir[] = {
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
	return ovl_redirect_dir_def	  ? OVL_REDIRECT_ON :
	       ovl_redirect_always_follow ? OVL_REDIRECT_FOLLOW :
					    OVL_REDIRECT_NOFOLLOW;
}

static const struct constant_table ovl_parameter_verity[] = {
	{ "off",	OVL_VERITY_OFF     },
	{ "on",		OVL_VERITY_ON      },
	{ "require",	OVL_VERITY_REQUIRE },
	{}
};

static const char *ovl_verity_mode(struct ovl_config *config)
{
	return ovl_parameter_verity[config->verity_mode].name;
}

static int ovl_verity_mode_def(void)
{
	return OVL_VERITY_OFF;
}

#define fsparam_string_empty(NAME, OPT) \
	__fsparam(fs_param_is_string, NAME, OPT, fs_param_can_be_empty, NULL)


const struct fs_parameter_spec ovl_parameter_spec[] = {
	fsparam_string_empty("lowerdir",    Opt_lowerdir),
	fsparam_string("lowerdir+",         Opt_lowerdir_add),
	fsparam_string("datadir+",          Opt_datadir_add),
	fsparam_string("upperdir",          Opt_upperdir),
	fsparam_string("workdir",           Opt_workdir),
	fsparam_flag("default_permissions", Opt_default_permissions),
	fsparam_enum("redirect_dir",        Opt_redirect_dir, ovl_parameter_redirect_dir),
	fsparam_enum("index",               Opt_index, ovl_parameter_bool),
	fsparam_enum("uuid",                Opt_uuid, ovl_parameter_uuid),
	fsparam_enum("nfs_export",          Opt_nfs_export, ovl_parameter_bool),
	fsparam_flag("userxattr",           Opt_userxattr),
	fsparam_enum("xino",                Opt_xino, ovl_parameter_xino),
	fsparam_enum("metacopy",            Opt_metacopy, ovl_parameter_bool),
	fsparam_enum("verity",              Opt_verity, ovl_parameter_verity),
	fsparam_flag("volatile",            Opt_volatile),
	{}
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

static int ovl_parse_monolithic(struct fs_context *fc, void *data)
{
	return vfs_parse_monolithic_sep(fc, data, ovl_next_opt);
}

static ssize_t ovl_parse_param_split_lowerdirs(char *str)
{
	ssize_t nr_layers = 1, nr_colons = 0;
	char *s, *d;

	for (s = d = str;; s++, d++) {
		if (*s == '\\') {
			/* keep esc chars in split lowerdir */
			*d++ = *s++;
		} else if (*s == ':') {
			bool next_colon = (*(s + 1) == ':');

			nr_colons++;
			if (nr_colons == 2 && next_colon) {
				pr_err("only single ':' or double '::' sequences of unescaped colons in lowerdir mount option allowed.\n");
				return -EINVAL;
			}
			/* count layers, not colons */
			if (!next_colon)
				nr_layers++;

			*d = '\0';
			continue;
		}

		*d = *s;
		if (!*s) {
			/* trailing colons */
			if (nr_colons) {
				pr_err("unescaped trailing colons in lowerdir mount option.\n");
				return -EINVAL;
			}
			break;
		}
		nr_colons = 0;
	}

	return nr_layers;
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
	return 0;

out:
	return err;
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

static int ovl_mount_dir(const char *name, struct path *path)
{
	int err = -ENOMEM;
	char *tmp = kstrdup(name, GFP_KERNEL);

	if (tmp) {
		ovl_unescape(tmp);
		err = ovl_mount_dir_noesc(tmp, path);
		kfree(tmp);
	}
	return err;
}

static int ovl_mount_dir_check(struct fs_context *fc, const struct path *path,
			       enum ovl_opt layer, const char *name, bool upper)
{
	struct ovl_fs_context *ctx = fc->fs_private;

	if (!d_is_dir(path->dentry))
		return invalfc(fc, "%s is not a directory", name);

	/*
	 * Root dentries of case-insensitive capable filesystems might
	 * not have the dentry operations set, but still be incompatible
	 * with overlayfs.  Check explicitly to prevent post-mount
	 * failures.
	 */
	if (sb_has_encoding(path->mnt->mnt_sb))
		return invalfc(fc, "case-insensitive capable filesystem on %s not supported", name);

	if (ovl_dentry_weird(path->dentry))
		return invalfc(fc, "filesystem on %s not supported", name);

	/*
	 * Check whether upper path is read-only here to report failures
	 * early. Don't forget to recheck when the superblock is created
	 * as the mount attributes could change.
	 */
	if (upper) {
		if (path->dentry->d_flags & DCACHE_OP_REAL)
			return invalfc(fc, "filesystem on %s not supported as upperdir", name);
		if (__mnt_is_readonly(path->mnt))
			return invalfc(fc, "filesystem on %s is read-only", name);
	} else {
		if (ctx->lowerdir_all && layer != Opt_lowerdir)
			return invalfc(fc, "lowerdir+ and datadir+ cannot follow lowerdir");
		if (ctx->nr_data && layer == Opt_lowerdir_add)
			return invalfc(fc, "regular lower layers cannot follow data layers");
		if (ctx->nr == OVL_MAX_STACK)
			return invalfc(fc, "too many lower directories, limit is %d",
				       OVL_MAX_STACK);
	}
	return 0;
}

static int ovl_ctx_realloc_lower(struct fs_context *fc)
{
	struct ovl_fs_context *ctx = fc->fs_private;
	struct ovl_fs_context_layer *l;
	size_t nr;

	if (ctx->nr < ctx->capacity)
		return 0;

	nr = min_t(size_t, max(4096 / sizeof(*l), ctx->capacity * 2),
		   OVL_MAX_STACK);
	l = krealloc_array(ctx->lower, nr, sizeof(*l), GFP_KERNEL_ACCOUNT);
	if (!l)
		return -ENOMEM;

	ctx->lower = l;
	ctx->capacity = nr;
	return 0;
}

static void ovl_add_layer(struct fs_context *fc, enum ovl_opt layer,
			 struct path *path, char **pname)
{
	struct ovl_fs *ofs = fc->s_fs_info;
	struct ovl_config *config = &ofs->config;
	struct ovl_fs_context *ctx = fc->fs_private;
	struct ovl_fs_context_layer *l;

	switch (layer) {
	case Opt_workdir:
		swap(config->workdir, *pname);
		swap(ctx->work, *path);
		break;
	case Opt_upperdir:
		swap(config->upperdir, *pname);
		swap(ctx->upper, *path);
		break;
	case Opt_datadir_add:
		ctx->nr_data++;
		fallthrough;
	case Opt_lowerdir_add:
		WARN_ON(ctx->nr >= ctx->capacity);
		l = &ctx->lower[ctx->nr++];
		memset(l, 0, sizeof(*l));
		swap(l->name, *pname);
		swap(l->path, *path);
		break;
	default:
		WARN_ON(1);
	}
}

static int ovl_parse_layer(struct fs_context *fc, struct fs_parameter *param,
			   enum ovl_opt layer)
{
	char *name = kstrdup(param->string, GFP_KERNEL);
	bool upper = (layer == Opt_upperdir || layer == Opt_workdir);
	struct path path;
	int err;

	if (!name)
		return -ENOMEM;

	if (upper)
		err = ovl_mount_dir(name, &path);
	else
		err = ovl_mount_dir_noesc(name, &path);
	if (err)
		goto out_free;

	err = ovl_mount_dir_check(fc, &path, layer, name, upper);
	if (err)
		goto out_put;

	if (!upper) {
		err = ovl_ctx_realloc_lower(fc);
		if (err)
			goto out_put;
	}

	/* Store the user provided path string in ctx to show in mountinfo */
	ovl_add_layer(fc, layer, &path, &name);

out_put:
	path_put(&path);
out_free:
	kfree(name);
	return err;
}

static void ovl_reset_lowerdirs(struct ovl_fs_context *ctx)
{
	struct ovl_fs_context_layer *l = ctx->lower;

	// Reset old user provided lowerdir string
	kfree(ctx->lowerdir_all);
	ctx->lowerdir_all = NULL;

	for (size_t nr = 0; nr < ctx->nr; nr++, l++) {
		path_put(&l->path);
		kfree(l->name);
		l->name = NULL;
	}
	ctx->nr = 0;
	ctx->nr_data = 0;
}

/*
 * Parse lowerdir= mount option:
 *
 * e.g.: lowerdir=/lower1:/lower2:/lower3::/data1::/data2
 *     Set "/lower1", "/lower2", and "/lower3" as lower layers and
 *     "/data1" and "/data2" as data lower layers. Any existing lower
 *     layers are replaced.
 */
static int ovl_parse_param_lowerdir(const char *name, struct fs_context *fc)
{
	int err;
	struct ovl_fs_context *ctx = fc->fs_private;
	struct ovl_fs_context_layer *l;
	char *dup = NULL, *iter;
	ssize_t nr_lower, nr;
	bool data_layer = false;

	/*
	 * Ensure we're backwards compatible with mount(2)
	 * by allowing relative paths.
	 */

	/* drop all existing lower layers */
	ovl_reset_lowerdirs(ctx);

	if (!*name)
		return 0;

	if (*name == ':') {
		pr_err("cannot append lower layer");
		return -EINVAL;
	}

	// Store user provided lowerdir string to show in mount options
	ctx->lowerdir_all = kstrdup(name, GFP_KERNEL);
	if (!ctx->lowerdir_all)
		return -ENOMEM;

	dup = kstrdup(name, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	err = -EINVAL;
	nr_lower = ovl_parse_param_split_lowerdirs(dup);
	if (nr_lower < 0)
		goto out_err;

	if (nr_lower > OVL_MAX_STACK) {
		pr_err("too many lower directories, limit is %d\n", OVL_MAX_STACK);
		goto out_err;
	}

	if (nr_lower > ctx->capacity) {
		err = -ENOMEM;
		l = krealloc_array(ctx->lower, nr_lower, sizeof(*ctx->lower),
				   GFP_KERNEL_ACCOUNT);
		if (!l)
			goto out_err;

		ctx->lower = l;
		ctx->capacity = nr_lower;
	}

	iter = dup;
	l = ctx->lower;
	for (nr = 0; nr < nr_lower; nr++, l++) {
		ctx->nr++;
		memset(l, 0, sizeof(*l));

		err = ovl_mount_dir(iter, &l->path);
		if (err)
			goto out_put;

		err = ovl_mount_dir_check(fc, &l->path, Opt_lowerdir, iter, false);
		if (err)
			goto out_put;

		err = -ENOMEM;
		l->name = kstrdup(iter, GFP_KERNEL_ACCOUNT);
		if (!l->name)
			goto out_put;

		if (data_layer)
			ctx->nr_data++;

		/* Calling strchr() again would overrun. */
		if (ctx->nr == nr_lower)
			break;

		err = -EINVAL;
		iter = strchr(iter, '\0') + 1;
		if (*iter) {
			/*
			 * This is a regular layer so we require that
			 * there are no data layers.
			 */
			if (ctx->nr_data > 0) {
				pr_err("regular lower layers cannot follow data lower layers");
				goto out_put;
			}

			data_layer = false;
			continue;
		}

		/* This is a data lower layer. */
		data_layer = true;
		iter++;
	}
	kfree(dup);
	return 0;

out_put:
	ovl_reset_lowerdirs(ctx);

out_err:
	kfree(dup);

	/* Intentionally don't realloc to a smaller size. */
	return err;
}

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
	case Opt_lowerdir_add:
	case Opt_datadir_add:
	case Opt_upperdir:
	case Opt_workdir:
		err = ovl_parse_layer(fc, param, opt);
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
	case Opt_verity:
		config->verity_mode = result.uint_32;
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

static int ovl_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, ovl_fill_super);
}

static inline void ovl_fs_context_free(struct ovl_fs_context *ctx)
{
	ovl_reset_lowerdirs(ctx);
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

static int ovl_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct ovl_fs *ofs = OVL_FS(sb);
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

static const struct fs_context_operations ovl_context_ops = {
	.parse_monolithic = ovl_parse_monolithic,
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
int ovl_init_fs_context(struct fs_context *fc)
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

	ofs->config.redirect_mode	= ovl_redirect_mode_def();
	ofs->config.index		= ovl_index_def;
	ofs->config.uuid		= ovl_uuid_def();
	ofs->config.nfs_export		= ovl_nfs_export_def;
	ofs->config.xino		= ovl_xino_def();
	ofs->config.metacopy		= ovl_metacopy_def;

	fc->s_fs_info		= ofs;
	fc->fs_private		= ctx;
	fc->ops			= &ovl_context_ops;
	return 0;

out_err:
	ovl_fs_context_free(ctx);
	return -ENOMEM;

}

void ovl_free_fs(struct ovl_fs *ofs)
{
	struct vfsmount **mounts;
	unsigned i;

	iput(ofs->workbasedir_trap);
	iput(ofs->workdir_trap);
	dput(ofs->whiteout);
	dput(ofs->workdir);
	if (ofs->workdir_locked)
		ovl_inuse_unlock(ofs->workbasedir);
	dput(ofs->workbasedir);
	if (ofs->upperdir_locked)
		ovl_inuse_unlock(ovl_upper_mnt(ofs)->mnt_root);

	/* Reuse ofs->config.lowerdirs as a vfsmount array before freeing it */
	mounts = (struct vfsmount **) ofs->config.lowerdirs;
	for (i = 0; i < ofs->numlayer; i++) {
		iput(ofs->layers[i].trap);
		kfree(ofs->config.lowerdirs[i]);
		mounts[i] = ofs->layers[i].mnt;
	}
	kern_unmount_array(mounts, ofs->numlayer);
	kfree(ofs->layers);
	for (i = 0; i < ofs->numfs; i++)
		free_anon_bdev(ofs->fs[i].pseudo_dev);
	kfree(ofs->fs);

	kfree(ofs->config.lowerdirs);
	kfree(ofs->config.upperdir);
	kfree(ofs->config.workdir);
	if (ofs->creator_cred)
		put_cred(ofs->creator_cred);
	kfree(ofs);
}

int ovl_fs_params_verify(const struct ovl_fs_context *ctx,
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

	if (!config->upperdir && config->uuid == OVL_UUID_ON) {
		pr_info("option \"uuid=on\" requires an upper fs, falling back to uuid=null.\n");
		config->uuid = OVL_UUID_NULL;
	}

	/* Resolve verity -> metacopy dependency */
	if (config->verity_mode && !config->metacopy) {
		/* Don't allow explicit specified conflicting combinations */
		if (set.metacopy) {
			pr_err("conflicting options: metacopy=off,verity=%s\n",
			       ovl_verity_mode(config));
			return -EINVAL;
		}
		/* Otherwise automatically enable metacopy. */
		config->metacopy = true;
	}

	/*
	 * This is to make the logic below simpler.  It doesn't make any other
	 * difference, since redirect_dir=on is only used for upper.
	 */
	if (!config->upperdir && config->redirect_mode == OVL_REDIRECT_FOLLOW)
		config->redirect_mode = OVL_REDIRECT_ON;

	/* Resolve verity -> metacopy -> redirect_dir dependency */
	if (config->metacopy && config->redirect_mode != OVL_REDIRECT_ON) {
		if (set.metacopy && set.redirect) {
			pr_err("conflicting options: metacopy=on,redirect_dir=%s\n",
			       ovl_redirect_mode(config));
			return -EINVAL;
		}
		if (config->verity_mode && set.redirect) {
			pr_err("conflicting options: verity=%s,redirect_dir=%s\n",
			       ovl_verity_mode(config), ovl_redirect_mode(config));
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

	/* Resolve nfs_export -> !metacopy && !verity dependency */
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
		} else if (config->verity_mode) {
			/*
			 * There was an explicit verity=.. that resulted
			 * in this conflict.
			 */
			pr_info("disabling nfs_export due to verity=%s\n",
				ovl_verity_mode(config));
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


	/* Resolve userxattr -> !redirect && !metacopy && !verity dependency */
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
		if (config->verity_mode) {
			pr_err("conflicting options: userxattr,verity=%s\n",
			       ovl_verity_mode(config));
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

/**
 * ovl_show_options
 * @m: the seq_file handle
 * @dentry: The dentry to query
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does not fail.
 */
int ovl_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct ovl_fs *ofs = OVL_FS(sb);
	size_t nr, nr_merged_lower, nr_lower = 0;
	char **lowerdirs = ofs->config.lowerdirs;

	/*
	 * lowerdirs[0] holds the colon separated list that user provided
	 * with lowerdir mount option.
	 * lowerdirs[1..numlayer] hold the lowerdir paths that were added
	 * using the lowerdir+ and datadir+ mount options.
	 * For now, we do not allow mixing the legacy lowerdir mount option
	 * with the new lowerdir+ and datadir+ mount options.
	 */
	if (lowerdirs[0]) {
		seq_show_option(m, "lowerdir", lowerdirs[0]);
	} else {
		nr_lower = ofs->numlayer;
		nr_merged_lower = nr_lower - ofs->numdatalayer;
	}
	for (nr = 1; nr < nr_lower; nr++) {
		if (nr < nr_merged_lower)
			seq_show_option(m, "lowerdir+", lowerdirs[nr]);
		else
			seq_show_option(m, "datadir+", lowerdirs[nr]);
	}
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
	if (ofs->config.uuid != ovl_uuid_def())
		seq_printf(m, ",uuid=%s", ovl_uuid_mode(&ofs->config));
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
	if (ofs->config.verity_mode != ovl_verity_mode_def())
		seq_printf(m, ",verity=%s",
			   ovl_verity_mode(&ofs->config));
	return 0;
}
