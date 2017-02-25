/*
 *
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

struct ovl_config {
	char *lowerdir;
	char *upperdir;
	char *workdir;
	bool default_permissions;
	bool redirect_dir;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	struct vfsmount *upper_mnt;
	unsigned numlower;
	struct vfsmount **lower_mnt;
	struct dentry *workdir;
	long namelen;
	/* pathnames of lower and upper dirs, for show_options */
	struct ovl_config config;
	/* creds of process who forced instantiation of super block */
	const struct cred *creator_cred;
};

/* private information held for every overlayfs dentry */
struct ovl_entry {
	struct dentry *__upperdentry;
	struct ovl_dir_cache *cache;
	union {
		struct {
			u64 version;
			const char *redirect;
			bool opaque;
		};
		struct rcu_head rcu;
	};
	unsigned numlower;
	struct path lowerstack[];
};

struct ovl_entry *ovl_alloc_entry(unsigned int numlower);

static inline struct dentry *ovl_upperdentry_dereference(struct ovl_entry *oe)
{
	return lockless_dereference(oe->__upperdentry);
}
