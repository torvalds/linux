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
#include <linux/errno.h>
#include <linux/security.h>

struct cred;
struct dentry;
struct file_operations;
struct file_system_type;
struct net;
struct user_namespace;

enum fs_context_purpose {
	FS_CONTEXT_FOR_MOUNT,		/* New superblock for explicit mount */
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
	struct file_system_type	*fs_type;
	void			*fs_private;	/* The filesystem's context */
	struct dentry		*root;		/* The root and superblock */
	struct user_namespace	*user_ns;	/* The user namespace for this mount */
	struct net		*net_ns;	/* The network namespace for this mount */
	const struct cred	*cred;		/* The mounter's credentials */
	const char		*source;	/* The source name (eg. dev path) */
	const char		*subtype;	/* The subtype to set on the superblock */
	void			*security;	/* Linux S&M options */
	unsigned int		sb_flags;	/* Proposed superblock flags (SB_*) */
	unsigned int		sb_flags_mask;	/* Superblock flags that were changed */
	enum fs_context_purpose	purpose:8;
	bool			need_free:1;	/* Need to call ops->free() */
};

/*
 * fs_context manipulation functions.
 */
extern struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
						unsigned int sb_flags);

extern int vfs_get_tree(struct fs_context *fc);
extern void put_fs_context(struct fs_context *fc);

#endif /* _LINUX_FS_CONTEXT_H */
