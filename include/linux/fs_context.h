/* Filesystem superblock creation and reconfiguration context.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_FS_CONTEXT_H
#define _LINUX_FS_CONTEXT_H

#include <linux/kernel.h>
#include <linux/refcount.h>
#include <linux/errno.h>
#include <linux/security.h>
#include <linux/mutex.h>

struct cred;
struct dentry;
struct file_operations;
struct file_system_type;
struct mnt_namespace;
struct net;
struct pid_namespace;
struct super_block;
struct user_namespace;
struct vfsmount;
struct path;

enum fs_context_purpose {
	FS_CONTEXT_FOR_MOUNT,		/* New superblock for explicit mount */
	FS_CONTEXT_FOR_SUBMOUNT,	/* New superblock for automatic submount */
	FS_CONTEXT_FOR_RECONFIGURE,	/* Superblock reconfiguration (remount) */
};

/*
 * Userspace usage phase for fsopen/fspick.
 */
enum fs_context_phase {
	FS_CONTEXT_CREATE_PARAMS,	/* Loading params for sb creation */
	FS_CONTEXT_CREATING,		/* A superblock is being created */
	FS_CONTEXT_AWAITING_MOUNT,	/* Superblock created, awaiting fsmount() */
	FS_CONTEXT_AWAITING_RECONF,	/* Awaiting initialisation for reconfiguration */
	FS_CONTEXT_RECONF_PARAMS,	/* Loading params for reconfiguration */
	FS_CONTEXT_RECONFIGURING,	/* Reconfiguring the superblock */
	FS_CONTEXT_FAILED,		/* Failed to correctly transition a context */
};

/*
 * Type of parameter value.
 */
enum fs_value_type {
	fs_value_is_undefined,
	fs_value_is_flag,		/* Value not given a value */
	fs_value_is_string,		/* Value is a string */
	fs_value_is_blob,		/* Value is a binary blob */
	fs_value_is_filename,		/* Value is a filename* + dirfd */
	fs_value_is_filename_empty,	/* Value is a filename* + dirfd + AT_EMPTY_PATH */
	fs_value_is_file,		/* Value is a file* */
};

/*
 * Configuration parameter.
 */
struct fs_parameter {
	const char		*key;		/* Parameter name */
	enum fs_value_type	type:8;		/* The type of value here */
	union {
		char		*string;
		void		*blob;
		struct filename	*name;
		struct file	*file;
	};
	size_t	size;
	int	dirfd;
};

/*
 * Filesystem context for holding the parameters used in the creation or
 * reconfiguration of a superblock.
 *
 * Superblock creation fills in ->root whereas reconfiguration begins with this
 * already set.
 *
 * See Documentation/filesystems/mounting.txt
 */
struct fs_context {
	const struct fs_context_operations *ops;
	struct mutex		uapi_mutex;	/* Userspace access mutex */
	struct file_system_type	*fs_type;
	void			*fs_private;	/* The filesystem's context */
	struct dentry		*root;		/* The root and superblock */
	struct user_namespace	*user_ns;	/* The user namespace for this mount */
	struct net		*net_ns;	/* The network namespace for this mount */
	const struct cred	*cred;		/* The mounter's credentials */
	struct fc_log		*log;		/* Logging buffer */
	const char		*source;	/* The source name (eg. dev path) */
	const char		*subtype;	/* The subtype to set on the superblock */
	void			*security;	/* Linux S&M options */
	void			*s_fs_info;	/* Proposed s_fs_info */
	unsigned int		sb_flags;	/* Proposed superblock flags (SB_*) */
	unsigned int		sb_flags_mask;	/* Superblock flags that were changed */
	unsigned int		lsm_flags;	/* Information flags from the fs to the LSM */
	enum fs_context_purpose	purpose:8;
	enum fs_context_phase	phase:8;	/* The phase the context is in */
	bool			need_free:1;	/* Need to call ops->free() */
	bool			global:1;	/* Goes into &init_user_ns */
};

struct fs_context_operations {
	void (*free)(struct fs_context *fc);
	int (*dup)(struct fs_context *fc, struct fs_context *src_fc);
	int (*parse_param)(struct fs_context *fc, struct fs_parameter *param);
	int (*parse_monolithic)(struct fs_context *fc, void *data);
	int (*get_tree)(struct fs_context *fc);
	int (*reconfigure)(struct fs_context *fc);
};

/*
 * fs_context manipulation functions.
 */
extern struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
						unsigned int sb_flags);
extern struct fs_context *fs_context_for_reconfigure(struct dentry *dentry,
						unsigned int sb_flags,
						unsigned int sb_flags_mask);
extern struct fs_context *fs_context_for_submount(struct file_system_type *fs_type,
						struct dentry *reference);

extern struct fs_context *vfs_dup_fs_context(struct fs_context *fc);
extern int vfs_parse_fs_param(struct fs_context *fc, struct fs_parameter *param);
extern int vfs_parse_fs_string(struct fs_context *fc, const char *key,
			       const char *value, size_t v_size);
extern int generic_parse_monolithic(struct fs_context *fc, void *data);
extern int vfs_get_tree(struct fs_context *fc);
extern void put_fs_context(struct fs_context *fc);

/*
 * sget() wrapper to be called from the ->get_tree() op.
 */
enum vfs_get_super_keying {
	vfs_get_single_super,	/* Only one such superblock may exist */
	vfs_get_keyed_super,	/* Superblocks with different s_fs_info keys may exist */
	vfs_get_independent_super, /* Multiple independent superblocks may exist */
};
extern int vfs_get_super(struct fs_context *fc,
			 enum vfs_get_super_keying keying,
			 int (*fill_super)(struct super_block *sb,
					   struct fs_context *fc));

extern const struct file_operations fscontext_fops;

/*
 * Mount error, warning and informational message logging.  This structure is
 * shareable between a mount and a subordinate mount.
 */
struct fc_log {
	refcount_t	usage;
	u8		head;		/* Insertion index in buffer[] */
	u8		tail;		/* Removal index in buffer[] */
	u8		need_free;	/* Mask of kfree'able items in buffer[] */
	struct module	*owner;		/* Owner module for strings that don't then need freeing */
	char		*buffer[8];
};

extern __attribute__((format(printf, 2, 3)))
void logfc(struct fs_context *fc, const char *fmt, ...);

/**
 * infof - Store supplementary informational message
 * @fc: The context in which to log the informational message
 * @fmt: The format string
 *
 * Store the supplementary informational message for the process if the process
 * has enabled the facility.
 */
#define infof(fc, fmt, ...) ({ logfc(fc, "i "fmt, ## __VA_ARGS__); })

/**
 * warnf - Store supplementary warning message
 * @fc: The context in which to log the error message
 * @fmt: The format string
 *
 * Store the supplementary warning message for the process if the process has
 * enabled the facility.
 */
#define warnf(fc, fmt, ...) ({ logfc(fc, "w "fmt, ## __VA_ARGS__); })

/**
 * errorf - Store supplementary error message
 * @fc: The context in which to log the error message
 * @fmt: The format string
 *
 * Store the supplementary error message for the process if the process has
 * enabled the facility.
 */
#define errorf(fc, fmt, ...) ({ logfc(fc, "e "fmt, ## __VA_ARGS__); })

/**
 * invalf - Store supplementary invalid argument error message
 * @fc: The context in which to log the error message
 * @fmt: The format string
 *
 * Store the supplementary error message for the process if the process has
 * enabled the facility and return -EINVAL.
 */
#define invalf(fc, fmt, ...) ({	errorf(fc, fmt, ## __VA_ARGS__); -EINVAL; })

#endif /* _LINUX_FS_CONTEXT_H */
