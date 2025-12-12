// SPDX-License-Identifier: GPL-2.0-or-later
/* Provide a way to create a superblock configuration context within the kernel
 * that allows a superblock to be set up prior to mounting.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include <linux/security.h>
#include <linux/mnt_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <net/net_namespace.h>
#include <asm/sections.h>
#include "mount.h"
#include "internal.h"

static const struct constant_table common_set_sb_flag[] = {
	{ "dirsync",	SB_DIRSYNC },
	{ "lazytime",	SB_LAZYTIME },
	{ "mand",	SB_MANDLOCK },
	{ "ro",		SB_RDONLY },
	{ "sync",	SB_SYNCHRONOUS },
	{ },
};

static const struct constant_table common_clear_sb_flag[] = {
	{ "async",	SB_SYNCHRONOUS },
	{ "nolazytime",	SB_LAZYTIME },
	{ "nomand",	SB_MANDLOCK },
	{ "rw",		SB_RDONLY },
	{ },
};

/*
 * Check for a common mount option that manipulates s_flags.
 */
static int vfs_parse_sb_flag(struct fs_context *fc, const char *key)
{
	unsigned int token;

	token = lookup_constant(common_set_sb_flag, key, 0);
	if (token) {
		fc->sb_flags |= token;
		fc->sb_flags_mask |= token;
		return 0;
	}

	token = lookup_constant(common_clear_sb_flag, key, 0);
	if (token) {
		fc->sb_flags &= ~token;
		fc->sb_flags_mask |= token;
		return 0;
	}

	return -ENOPARAM;
}

/**
 * vfs_parse_fs_param_source - Handle setting "source" via parameter
 * @fc: The filesystem context to modify
 * @param: The parameter
 *
 * This is a simple helper for filesystems to verify that the "source" they
 * accept is sane.
 *
 * Returns 0 on success, -ENOPARAM if this is not  "source" parameter, and
 * -EINVAL otherwise. In the event of failure, supplementary error information
 *  is logged.
 */
int vfs_parse_fs_param_source(struct fs_context *fc, struct fs_parameter *param)
{
	if (strcmp(param->key, "source") != 0)
		return -ENOPARAM;

	if (param->type != fs_value_is_string)
		return invalf(fc, "Non-string source");

	if (fc->source)
		return invalf(fc, "Multiple sources");

	fc->source = param->string;
	param->string = NULL;
	return 0;
}
EXPORT_SYMBOL(vfs_parse_fs_param_source);

/**
 * vfs_parse_fs_param - Add a single parameter to a superblock config
 * @fc: The filesystem context to modify
 * @param: The parameter
 *
 * A single mount option in string form is applied to the filesystem context
 * being set up.  Certain standard options (for example "ro") are translated
 * into flag bits without going to the filesystem.  The active security module
 * is allowed to observe and poach options.  Any other options are passed over
 * to the filesystem to parse.
 *
 * This may be called multiple times for a context.
 *
 * Returns 0 on success and a negative error code on failure.  In the event of
 * failure, supplementary error information may have been set.
 */
int vfs_parse_fs_param(struct fs_context *fc, struct fs_parameter *param)
{
	int ret;

	if (!param->key)
		return invalf(fc, "Unnamed parameter\n");

	ret = vfs_parse_sb_flag(fc, param->key);
	if (ret != -ENOPARAM)
		return ret;

	ret = security_fs_context_parse_param(fc, param);
	if (ret != -ENOPARAM)
		/* Param belongs to the LSM or is disallowed by the LSM; so
		 * don't pass to the FS.
		 */
		return ret;

	if (fc->ops->parse_param) {
		ret = fc->ops->parse_param(fc, param);
		if (ret != -ENOPARAM)
			return ret;
	}

	/* If the filesystem doesn't take any arguments, give it the
	 * default handling of source.
	 */
	ret = vfs_parse_fs_param_source(fc, param);
	if (ret != -ENOPARAM)
		return ret;

	return invalf(fc, "%s: Unknown parameter '%s'",
		      fc->fs_type->name, param->key);
}
EXPORT_SYMBOL(vfs_parse_fs_param);

/**
 * vfs_parse_fs_qstr - Convenience function to just parse a string.
 * @fc: Filesystem context.
 * @key: Parameter name.
 * @value: Default value.
 */
int vfs_parse_fs_qstr(struct fs_context *fc, const char *key,
			const struct qstr *value)
{
	int ret;

	struct fs_parameter param = {
		.key	= key,
		.type	= fs_value_is_flag,
		.size	= value ? value->len : 0,
	};

	if (value) {
		param.string = kmemdup_nul(value->name, value->len, GFP_KERNEL);
		if (!param.string)
			return -ENOMEM;
		param.type = fs_value_is_string;
	}

	ret = vfs_parse_fs_param(fc, &param);
	kfree(param.string);
	return ret;
}
EXPORT_SYMBOL(vfs_parse_fs_qstr);

/**
 * vfs_parse_monolithic_sep - Parse key[=val][,key[=val]]* mount data
 * @fc: The superblock configuration to fill in.
 * @data: The data to parse
 * @sep: callback for separating next option
 *
 * Parse a blob of data that's in key[=val][,key[=val]]* form with a custom
 * option separator callback.
 *
 * Returns 0 on success or the error returned by the ->parse_option() fs_context
 * operation on failure.
 */
int vfs_parse_monolithic_sep(struct fs_context *fc, void *data,
			     char *(*sep)(char **))
{
	char *options = data, *key;
	int ret = 0;

	if (!options)
		return 0;

	ret = security_sb_eat_lsm_opts(options, &fc->security);
	if (ret)
		return ret;

	while ((key = sep(&options)) != NULL) {
		if (*key) {
			char *value = strchr(key, '=');

			if (value) {
				if (unlikely(value == key))
					continue;
				*value++ = 0;
			}
			ret = vfs_parse_fs_string(fc, key, value);
			if (ret < 0)
				break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(vfs_parse_monolithic_sep);

static char *vfs_parse_comma_sep(char **s)
{
	return strsep(s, ",");
}

/**
 * generic_parse_monolithic - Parse key[=val][,key[=val]]* mount data
 * @fc: The superblock configuration to fill in.
 * @data: The data to parse
 *
 * Parse a blob of data that's in key[=val][,key[=val]]* form.  This can be
 * called from the ->monolithic_mount_data() fs_context operation.
 *
 * Returns 0 on success or the error returned by the ->parse_option() fs_context
 * operation on failure.
 */
int generic_parse_monolithic(struct fs_context *fc, void *data)
{
	return vfs_parse_monolithic_sep(fc, data, vfs_parse_comma_sep);
}
EXPORT_SYMBOL(generic_parse_monolithic);

/**
 * alloc_fs_context - Create a filesystem context.
 * @fs_type: The filesystem type.
 * @reference: The dentry from which this one derives (or NULL)
 * @sb_flags: Filesystem/superblock flags (SB_*)
 * @sb_flags_mask: Applicable members of @sb_flags
 * @purpose: The purpose that this configuration shall be used for.
 *
 * Open a filesystem and create a mount context.  The mount context is
 * initialised with the supplied flags and, if a submount/automount from
 * another superblock (referred to by @reference) is supplied, may have
 * parameters such as namespaces copied across from that superblock.
 */
static struct fs_context *alloc_fs_context(struct file_system_type *fs_type,
				      struct dentry *reference,
				      unsigned int sb_flags,
				      unsigned int sb_flags_mask,
				      enum fs_context_purpose purpose)
{
	struct fs_context *fc;
	int ret = -ENOMEM;

	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL_ACCOUNT);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->purpose	= purpose;
	fc->sb_flags	= sb_flags;
	fc->sb_flags_mask = sb_flags_mask;
	fc->fs_type	= get_filesystem(fs_type);
	fc->cred	= get_current_cred();
	fc->net_ns	= get_net(current->nsproxy->net_ns);
	fc->log.prefix	= fs_type->name;

	mutex_init(&fc->uapi_mutex);

	switch (purpose) {
	case FS_CONTEXT_FOR_MOUNT:
		fc->user_ns = get_user_ns(fc->cred->user_ns);
		break;
	case FS_CONTEXT_FOR_SUBMOUNT:
		fc->user_ns = get_user_ns(reference->d_sb->s_user_ns);
		break;
	case FS_CONTEXT_FOR_RECONFIGURE:
		atomic_inc(&reference->d_sb->s_active);
		fc->user_ns = get_user_ns(reference->d_sb->s_user_ns);
		fc->root = dget(reference);
		break;
	}

	ret = fc->fs_type->init_fs_context(fc);
	if (ret < 0)
		goto err_fc;
	fc->need_free = true;
	return fc;

err_fc:
	put_fs_context(fc);
	return ERR_PTR(ret);
}

struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
					unsigned int sb_flags)
{
	return alloc_fs_context(fs_type, NULL, sb_flags, 0,
					FS_CONTEXT_FOR_MOUNT);
}
EXPORT_SYMBOL(fs_context_for_mount);

struct fs_context *fs_context_for_reconfigure(struct dentry *dentry,
					unsigned int sb_flags,
					unsigned int sb_flags_mask)
{
	return alloc_fs_context(dentry->d_sb->s_type, dentry, sb_flags,
				sb_flags_mask, FS_CONTEXT_FOR_RECONFIGURE);
}
EXPORT_SYMBOL(fs_context_for_reconfigure);

/**
 * fs_context_for_submount: allocate a new fs_context for a submount
 * @type: file_system_type of the new context
 * @reference: reference dentry from which to copy relevant info
 *
 * Allocate a new fs_context suitable for a submount. This also ensures that
 * the fc->security object is inherited from @reference (if needed).
 */
struct fs_context *fs_context_for_submount(struct file_system_type *type,
					   struct dentry *reference)
{
	struct fs_context *fc;
	int ret;

	fc = alloc_fs_context(type, reference, 0, 0, FS_CONTEXT_FOR_SUBMOUNT);
	if (IS_ERR(fc))
		return fc;

	ret = security_fs_context_submount(fc, reference->d_sb);
	if (ret) {
		put_fs_context(fc);
		return ERR_PTR(ret);
	}

	return fc;
}
EXPORT_SYMBOL(fs_context_for_submount);

void fc_drop_locked(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	dput(fc->root);
	fc->root = NULL;
	deactivate_locked_super(sb);
}

/**
 * vfs_dup_fs_context - Duplicate a filesystem context.
 * @src_fc: The context to copy.
 */
struct fs_context *vfs_dup_fs_context(struct fs_context *src_fc)
{
	struct fs_context *fc;
	int ret;

	if (!src_fc->ops->dup)
		return ERR_PTR(-EOPNOTSUPP);

	fc = kmemdup(src_fc, sizeof(struct fs_context), GFP_KERNEL);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	mutex_init(&fc->uapi_mutex);

	fc->fs_private	= NULL;
	fc->s_fs_info	= NULL;
	fc->source	= NULL;
	fc->security	= NULL;
	get_filesystem(fc->fs_type);
	get_net(fc->net_ns);
	get_user_ns(fc->user_ns);
	get_cred(fc->cred);
	if (fc->log.log)
		refcount_inc(&fc->log.log->usage);

	/* Can't call put until we've called ->dup */
	ret = fc->ops->dup(fc, src_fc);
	if (ret < 0)
		goto err_fc;

	ret = security_fs_context_dup(fc, src_fc);
	if (ret < 0)
		goto err_fc;
	return fc;

err_fc:
	put_fs_context(fc);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(vfs_dup_fs_context);

/**
 * logfc - Log a message to a filesystem context
 * @log: The filesystem context to log to, or NULL to use printk.
 * @prefix: A string to prefix the output with, or NULL.
 * @level: 'w' for a warning, 'e' for an error.  Anything else is a notice.
 * @fmt: The format of the buffer.
 */
void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf = {.fmt = fmt, .va = &va};

	va_start(va, fmt);
	if (!log) {
		switch (level) {
		case 'w':
			printk(KERN_WARNING "%s%s%pV\n", prefix ? prefix : "",
						prefix ? ": " : "", &vaf);
			break;
		case 'e':
			printk(KERN_ERR "%s%s%pV\n", prefix ? prefix : "",
						prefix ? ": " : "", &vaf);
			break;
		case 'i':
			printk(KERN_INFO "%s%s%pV\n", prefix ? prefix : "",
						prefix ? ": " : "", &vaf);
			break;
		default:
			printk(KERN_NOTICE "%s%s%pV\n", prefix ? prefix : "",
						prefix ? ": " : "", &vaf);
			break;
		}
	} else {
		unsigned int logsize = ARRAY_SIZE(log->buffer);
		u8 index;
		char *q = kasprintf(GFP_KERNEL, "%c %s%s%pV\n", level,
						prefix ? prefix : "",
						prefix ? ": " : "", &vaf);

		index = log->head & (logsize - 1);
		BUILD_BUG_ON(sizeof(log->head) != sizeof(u8) ||
			     sizeof(log->tail) != sizeof(u8));
		if ((u8)(log->head - log->tail) == logsize) {
			/* The buffer is full, discard the oldest message */
			if (log->need_free & (1 << index))
				kfree(log->buffer[index]);
			log->tail++;
		}

		log->buffer[index] = q ? q : "OOM: Can't store error string";
		if (q)
			log->need_free |= 1 << index;
		else
			log->need_free &= ~(1 << index);
		log->head++;
	}
	va_end(va);
}
EXPORT_SYMBOL(logfc);

/*
 * Free a logging structure.
 */
static void put_fc_log(struct fs_context *fc)
{
	struct fc_log *log = fc->log.log;
	int i;

	if (log) {
		if (refcount_dec_and_test(&log->usage)) {
			fc->log.log = NULL;
			for (i = 0; i < ARRAY_SIZE(log->buffer) ; i++)
				if (log->need_free & (1 << i))
					kfree(log->buffer[i]);
			kfree(log);
		}
	}
}

/**
 * put_fs_context - Dispose of a superblock configuration context.
 * @fc: The context to dispose of.
 */
void put_fs_context(struct fs_context *fc)
{
	struct super_block *sb;

	if (fc->root) {
		sb = fc->root->d_sb;
		dput(fc->root);
		fc->root = NULL;
		deactivate_super(sb);
	}

	if (fc->need_free && fc->ops && fc->ops->free)
		fc->ops->free(fc);

	security_free_mnt_opts(&fc->security);
	put_net(fc->net_ns);
	put_user_ns(fc->user_ns);
	put_cred(fc->cred);
	put_fc_log(fc);
	put_filesystem(fc->fs_type);
	kfree(fc->source);
	kfree(fc);
}
EXPORT_SYMBOL(put_fs_context);

int parse_monolithic_mount_data(struct fs_context *fc, void *data)
{
	int (*monolithic_mount_data)(struct fs_context *, void *);

	monolithic_mount_data = fc->ops->parse_monolithic;
	if (!monolithic_mount_data)
		monolithic_mount_data = generic_parse_monolithic;

	return monolithic_mount_data(fc, data);
}

/*
 * Clean up a context after performing an action on it and put it into a state
 * from where it can be used to reconfigure a superblock.
 *
 * Note that here we do only the parts that can't fail; the rest is in
 * finish_clean_context() below and in between those fs_context is marked
 * FS_CONTEXT_AWAITING_RECONF.  The reason for splitup is that after
 * successful mount or remount we need to report success to userland.
 * Trying to do full reinit (for the sake of possible subsequent remount)
 * and failing to allocate memory would've put us into a nasty situation.
 * So here we only discard the old state and reinitialization is left
 * until we actually try to reconfigure.
 */
void vfs_clean_context(struct fs_context *fc)
{
	if (fc->need_free && fc->ops && fc->ops->free)
		fc->ops->free(fc);
	fc->need_free = false;
	fc->fs_private = NULL;
	fc->s_fs_info = NULL;
	fc->sb_flags = 0;
	security_free_mnt_opts(&fc->security);
	kfree(fc->source);
	fc->source = NULL;
	fc->exclusive = false;

	fc->purpose = FS_CONTEXT_FOR_RECONFIGURE;
	fc->phase = FS_CONTEXT_AWAITING_RECONF;
}

int finish_clean_context(struct fs_context *fc)
{
	int error;

	if (fc->phase != FS_CONTEXT_AWAITING_RECONF)
		return 0;

	error = fc->fs_type->init_fs_context(fc);

	if (unlikely(error)) {
		fc->phase = FS_CONTEXT_FAILED;
		return error;
	}
	fc->need_free = true;
	fc->phase = FS_CONTEXT_RECONF_PARAMS;
	return 0;
}
