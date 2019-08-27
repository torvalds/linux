/*
 * fs/sdcardfs/main.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/parser.h>

enum sdcardfs_param {
	Opt_fsuid,
	Opt_fsgid,
	Opt_gid,
	Opt_debug,
	Opt_mask,
	Opt_multiuser,
	Opt_userid,
	Opt_reserved_mb,
	Opt_gid_derivation,
	Opt_default_normal,
	Opt_nocache,
	Opt_unshared_obb,
	Opt_err,
};

static const struct fs_parameter_spec sdcardfs_param_specs[] = {
	fsparam_u32("fsuid", Opt_fsuid),
	fsparam_u32("fsgid", Opt_fsgid),
	fsparam_u32("gid", Opt_gid),
	fsparam_bool("debug", Opt_debug),
	fsparam_u32("mask", Opt_mask),
	fsparam_u32("userid", Opt_userid),
	fsparam_bool("multiuser", Opt_multiuser),
	fsparam_bool("derive_gid", Opt_gid_derivation),
	fsparam_bool("default_normal", Opt_default_normal),
	fsparam_bool("unshared_obb", Opt_unshared_obb),
	fsparam_u32("reserved_mb", Opt_reserved_mb),
	fsparam_bool("nocache", Opt_nocache),
	{}
};

static const struct fs_parameter_description sdcardfs_parameters = {
	.name	= "sdcardfs",
	.specs	= sdcardfs_param_specs,
};

static int sdcardfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct sdcardfs_context_options *fc_opts = fc->fs_private;
	struct sdcardfs_mount_options *opts = &fc_opts->opts;
	struct sdcardfs_vfsmount_options *vfsopts = &fc_opts->vfsopts;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, &sdcardfs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_debug:
		opts->debug = true;
		break;
	case Opt_fsuid:
		opts->fs_low_uid = result.uint_32;
		break;
	case Opt_fsgid:
		opts->fs_low_gid = result.uint_32;
		break;
	case Opt_gid:
		vfsopts->gid = result.uint_32;
		break;
	case Opt_userid:
		opts->fs_user_id = result.uint_32;
		break;
	case Opt_mask:
		vfsopts->mask = result.uint_32;
		break;
	case Opt_multiuser:
		opts->multiuser = true;
		break;
	case Opt_reserved_mb:
		opts->reserved_mb = result.uint_32;
		break;
	case Opt_gid_derivation:
		opts->gid_derivation = true;
		break;
	case Opt_default_normal:
		opts->default_normal = true;
		break;
	case Opt_nocache:
		opts->nocache = true;
		break;
	case Opt_unshared_obb:
		opts->unshared_obb = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void copy_sb_opts(struct sdcardfs_mount_options *opts,
		struct fs_context *fc)
{
	struct sdcardfs_context_options *fcopts = fc->fs_private;

	opts->debug = fcopts->opts.debug;
	opts->default_normal = fcopts->opts.default_normal;
	opts->fs_low_gid = fcopts->opts.fs_low_gid;
	opts->fs_low_uid = fcopts->opts.fs_low_uid;
	opts->fs_user_id = fcopts->opts.fs_user_id;
	opts->gid_derivation = fcopts->opts.gid_derivation;
	opts->multiuser = fcopts->opts.multiuser;
	opts->nocache = fcopts->opts.nocache;
	opts->reserved_mb = fcopts->opts.reserved_mb;
	opts->unshared_obb = fcopts->opts.unshared_obb;
}

#if 0
/*
 * our custom d_alloc_root work-alike
 *
 * we can't use d_alloc_root if we want to use our own interpose function
 * unchanged, so we simply call our own "fake" d_alloc_root
 */
static struct dentry *sdcardfs_d_alloc_root(struct super_block *sb)
{
	struct dentry *ret = NULL;

	if (sb) {
		static const struct qstr name = {
			.name = "/",
			.len = 1
		};

		ret = d_alloc(NULL, &name);
		if (ret) {
			d_set_d_op(ret, &sdcardfs_ci_dops);
			ret->d_sb = sb;
			ret->d_parent = ret;
		}
	}
	return ret;
}
#endif

DEFINE_MUTEX(sdcardfs_super_list_lock);
EXPORT_SYMBOL_GPL(sdcardfs_super_list_lock);
LIST_HEAD(sdcardfs_super_list);
EXPORT_SYMBOL_GPL(sdcardfs_super_list);

struct sdcardfs_mount_private {
	struct vfsmount *mnt;
	const char *dev_name;
	void *raw_data;
};

static int __sdcardfs_fill_super(
	struct super_block *sb,
	struct fs_context *fc)
{
	int err = 0;
	struct super_block *lower_sb;
	struct path lower_path;
	struct sdcardfs_sb_info *sb_info;
	struct inode *inode;
	const char *dev_name = fc->source;
	struct sdcardfs_context_options *fcopts = fc->fs_private;
	struct sdcardfs_mount_options *opts = &fcopts->opts;
	struct sdcardfs_vfsmount_options *mntopts = &fcopts->vfsopts;

	pr_info("sdcardfs version 2.0\n");

	if (!dev_name) {
		pr_err("sdcardfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}

	pr_info("sdcardfs: dev_name -> %s\n", dev_name);
	pr_info("sdcardfs: gid=%d,mask=%x\n", mntopts->gid, mntopts->mask);

	/* parse lower path */
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		pr_err("sdcardfs: error accessing lower directory '%s'\n", dev_name);
		goto out;
	}

	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct sdcardfs_sb_info), GFP_KERNEL);
	if (!SDCARDFS_SB(sb)) {
		pr_crit("sdcardfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}

	sb_info = sb->s_fs_info;
	copy_sb_opts(&sb_info->options, fc);
	if (opts->debug) {
		pr_info("sdcardfs : options - debug:%d\n", opts->debug);
		pr_info("sdcardfs : options - gid:%d\n", mntopts->gid);
		pr_info("sdcardfs : options - mask:%d\n", mntopts->mask);
	}

	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	sdcardfs_set_lower_super(sb, lower_sb);

	sb->s_stack_depth = lower_sb->s_stack_depth + 1;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("sdcardfs: maximum fs stacking depth exceeded\n");
		err = -EINVAL;
		goto out_sput;
	}

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_magic = SDCARDFS_SUPER_MAGIC;
	sb->s_op = &sdcardfs_sops;

	/* get a new inode and allocate our root dentry */
	inode = sdcardfs_iget(sb, d_inode(lower_path.dentry), 0);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_sput;
	}
	d_set_d_op(sb->s_root, &sdcardfs_ci_dops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* set the lower dentries for s_root */
	sdcardfs_set_lower_path(sb->s_root, &lower_path);

	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_make_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);

	/* setup permission policy */
	sb_info->obbpath_s = kzalloc(PATH_MAX, GFP_KERNEL);
	mutex_lock(&sdcardfs_super_list_lock);
	if (sb_info->options.multiuser) {
		setup_derived_state(d_inode(sb->s_root), PERM_PRE_ROOT,
				sb_info->options.fs_user_id, AID_ROOT);
		snprintf(sb_info->obbpath_s, PATH_MAX, "%s/obb", dev_name);
	} else {
		setup_derived_state(d_inode(sb->s_root), PERM_ROOT,
				sb_info->options.fs_user_id, AID_ROOT);
		snprintf(sb_info->obbpath_s, PATH_MAX, "%s/Android/obb", dev_name);
	}
	fixup_tmp_permissions(d_inode(sb->s_root));
	sb_info->sb = sb;
	list_add(&sb_info->list, &sdcardfs_super_list);
	mutex_unlock(&sdcardfs_super_list_lock);

	if (!(fc->sb_flags & SB_SILENT))
		pr_info("sdcardfs: mounted on top of %s type %s\n",
				dev_name, lower_sb->s_type->name);
	goto out; /* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	dput(sb->s_root);
	sb->s_root = NULL;
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
	kfree(SDCARDFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);

out:
	return err;
}

static int sdcardfs_get_tree(struct fs_context *fc)
{
	return vfs_get_super(fc, vfs_get_independent_super,
			__sdcardfs_fill_super);
}

void *sdcardfs_alloc_mnt_data(void)
{
	return kmalloc(sizeof(struct sdcardfs_vfsmount_options), GFP_KERNEL);
}

void sdcardfs_kill_sb(struct super_block *sb)
{
	struct sdcardfs_sb_info *sbi;

	if (sb->s_magic == SDCARDFS_SUPER_MAGIC && sb->s_fs_info) {
		sbi = SDCARDFS_SB(sb);
		mutex_lock(&sdcardfs_super_list_lock);
		list_del(&sbi->list);
		mutex_unlock(&sdcardfs_super_list_lock);
	}
	kill_anon_super(sb);
}

static void sdcardfs_free_fs_context(struct fs_context *fc)
{
	struct sdcardfs_context_options *fc_opts = fc->fs_private;

	kfree(fc_opts);
}

/* Most of the remount happens in sdcardfs_update_mnt_data */
static int sdcardfs_reconfigure_context(struct fs_context *fc)
{
	struct sdcardfs_context_options *fc_opts = fc->fs_private;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(fc->root->d_sb);

	sbi->options.debug = fc_opts->opts.debug;
	if (sbi->options.debug) {
		pr_info("sdcardfs : options - debug:%d\n", sbi->options.debug);
		pr_info("sdcardfs : options - gid:%d\n", fc_opts->vfsopts.gid);
		pr_info("sdcardfs : options - mask:%d\n",
						fc_opts->vfsopts.mask);
	}
	return 0;
}

/* reconfigure is handled by sdcardfs_update_mnt_data */
static const struct fs_context_operations sdcardfs_context_options_ops = {

    .parse_param    = sdcardfs_parse_param,
    .get_tree   = sdcardfs_get_tree,
    .free       = sdcardfs_free_fs_context,
    .reconfigure = sdcardfs_reconfigure_context,
};

static int sdcardfs_init_fs_context(struct fs_context *fc)
{
	struct sdcardfs_context_options *fc_opts =
		kmalloc(sizeof(struct sdcardfs_context_options), GFP_KERNEL);

	/* by default, we use AID_MEDIA_RW as uid, gid */
	fc_opts->opts.fs_low_uid = AID_MEDIA_RW;
	fc_opts->opts.fs_low_gid = AID_MEDIA_RW;
	fc_opts->opts.fs_user_id = 0;
	fc_opts->vfsopts.gid = 0;
	fc_opts->vfsopts.mask = 0;

	/* by default, 0MB is reserved */
	fc_opts->opts.reserved_mb = 0;
	/* by default, gid derivation is off */
	fc_opts->opts.gid_derivation = false;
	fc_opts->opts.default_normal = false;
	fc_opts->opts.nocache = false;
	fc_opts->opts.multiuser = false;
	fc_opts->opts.debug = false;

	fc->fs_private = fc_opts;
	fc->ops = &sdcardfs_context_options_ops;
	return 0;
}

static struct file_system_type sdcardfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= SDCARDFS_NAME,
	.alloc_mnt_data = sdcardfs_alloc_mnt_data,
	.kill_sb	= sdcardfs_kill_sb,
	.init_fs_context = sdcardfs_init_fs_context,
	.fs_flags	= 0,
};
MODULE_ALIAS_FS(SDCARDFS_NAME);

static int __init init_sdcardfs_fs(void)
{
	int err;

	pr_info("Registering sdcardfs " SDCARDFS_VERSION "\n");

	err = sdcardfs_init_inode_cache();
	if (err)
		goto out;
	err = sdcardfs_init_dentry_cache();
	if (err)
		goto out;
	err = packagelist_init();
	if (err)
		goto out;
	err = register_filesystem(&sdcardfs_fs_type);
out:
	if (err) {
		sdcardfs_destroy_inode_cache();
		sdcardfs_destroy_dentry_cache();
		packagelist_exit();
	}
	return err;
}

static void __exit exit_sdcardfs_fs(void)
{
	sdcardfs_destroy_inode_cache();
	sdcardfs_destroy_dentry_cache();
	packagelist_exit();
	unregister_filesystem(&sdcardfs_fs_type);
	pr_info("Completed sdcardfs module unload\n");
}

/* Original wrapfs authors */
MODULE_AUTHOR("Erez Zadok, Filesystems and Storage Lab, Stony Brook University (http://www.fsl.cs.sunysb.edu/)");

/* Original sdcardfs authors */
MODULE_AUTHOR("Woojoong Lee, Daeho Jeong, Kitae Lee, Yeongjin Gil System Memory Lab., Samsung Electronics");

/* Current maintainer */
MODULE_AUTHOR("Daniel Rosenberg, Google");
MODULE_DESCRIPTION("Sdcardfs " SDCARDFS_VERSION);
MODULE_LICENSE("GPL");

module_init(init_sdcardfs_fs);
module_exit(exit_sdcardfs_fs);
