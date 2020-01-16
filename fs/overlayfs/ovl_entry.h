/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 */

struct ovl_config {
	char *lowerdir;
	char *upperdir;
	char *workdir;
	bool default_permissions;
	bool redirect_dir;
	bool redirect_follow;
	const char *redirect_mode;
	bool index;
	bool nfs_export;
	int xiyes;
	bool metacopy;
};

struct ovl_sb {
	struct super_block *sb;
	dev_t pseudo_dev;
	/* Unusable (conflicting) uuid */
	bool bad_uuid;
};

struct ovl_layer {
	struct vfsmount *mnt;
	/* Trap in ovl iyesde cache */
	struct iyesde *trap;
	struct ovl_sb *fs;
	/* Index of this layer in fs root (upper idx == 0) */
	int idx;
	/* One fsid per unique underlying sb (upper fsid == 0) */
	int fsid;
};

struct ovl_path {
	struct ovl_layer *layer;
	struct dentry *dentry;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	struct vfsmount *upper_mnt;
	unsigned int numlower;
	/* Number of unique lower sb that differ from upper sb */
	unsigned int numlowerfs;
	struct ovl_layer *lower_layers;
	struct ovl_sb *lower_fs;
	/* workbasedir is the path at workdir= mount option */
	struct dentry *workbasedir;
	/* workdir is the 'work' directory under workbasedir */
	struct dentry *workdir;
	/* index directory listing overlay iyesdes by origin file handle */
	struct dentry *indexdir;
	long namelen;
	/* pathnames of lower and upper dirs, for show_options */
	struct ovl_config config;
	/* creds of process who forced instantiation of super block */
	const struct cred *creator_cred;
	bool tmpfile;
	bool yesxattr;
	/* Did we take the inuse lock? */
	bool upperdir_locked;
	bool workdir_locked;
	/* Traps in ovl iyesde cache */
	struct iyesde *upperdir_trap;
	struct iyesde *workbasedir_trap;
	struct iyesde *workdir_trap;
	struct iyesde *indexdir_trap;
	/* Iyesde numbers in all layers do yest use the high xiyes_bits */
	unsigned int xiyes_bits;
};

/* private information held for every overlayfs dentry */
struct ovl_entry {
	union {
		struct {
			unsigned long flags;
		};
		struct rcu_head rcu;
	};
	unsigned numlower;
	struct ovl_path lowerstack[];
};

struct ovl_entry *ovl_alloc_entry(unsigned int numlower);

static inline struct ovl_entry *OVL_E(struct dentry *dentry)
{
	return (struct ovl_entry *) dentry->d_fsdata;
}

struct ovl_iyesde {
	union {
		struct ovl_dir_cache *cache;	/* directory */
		struct iyesde *lowerdata;	/* regular file */
	};
	const char *redirect;
	u64 version;
	unsigned long flags;
	struct iyesde vfs_iyesde;
	struct dentry *__upperdentry;
	struct iyesde *lower;

	/* synchronize copy up and more */
	struct mutex lock;
};

static inline struct ovl_iyesde *OVL_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct ovl_iyesde, vfs_iyesde);
}

static inline struct dentry *ovl_upperdentry_dereference(struct ovl_iyesde *oi)
{
	return READ_ONCE(oi->__upperdentry);
}
