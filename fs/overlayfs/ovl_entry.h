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
	bool redirect_follow;
	const char *redirect_mode;
	bool index;
	bool nfs_export;
	int xino;
	bool metacopy;
};

struct ovl_sb {
	struct super_block *sb;
	dev_t pseudo_dev;
};

struct ovl_layer {
	struct vfsmount *mnt;
	/* Trap in ovl inode cache */
	struct inode *trap;
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
	/* index directory listing overlay inodes by origin file handle */
	struct dentry *indexdir;
	long namelen;
	/* pathnames of lower and upper dirs, for show_options */
	struct ovl_config config;
	/* creds of process who forced instantiation of super block */
	const struct cred *creator_cred;
	bool tmpfile;
	bool noxattr;
	/* Did we take the inuse lock? */
	bool upperdir_locked;
	bool workdir_locked;
	/* Traps in ovl inode cache */
	struct inode *upperdir_trap;
	struct inode *workbasedir_trap;
	struct inode *workdir_trap;
	struct inode *indexdir_trap;
	/* Inode numbers in all layers do not use the high xino_bits */
	unsigned int xino_bits;
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

struct ovl_inode {
	union {
		struct ovl_dir_cache *cache;	/* directory */
		struct inode *lowerdata;	/* regular file */
	};
	const char *redirect;
	u64 version;
	unsigned long flags;
	struct inode vfs_inode;
	struct dentry *__upperdentry;
	struct inode *lower;

	/* synchronize copy up and more */
	struct mutex lock;
};

static inline struct ovl_inode *OVL_I(struct inode *inode)
{
	return container_of(inode, struct ovl_inode, vfs_inode);
}

static inline struct dentry *ovl_upperdentry_dereference(struct ovl_inode *oi)
{
	return READ_ONCE(oi->__upperdentry);
}
