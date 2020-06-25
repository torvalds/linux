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
	int xino;
	bool metacopy;
	bool override_creds;
};

struct ovl_sb {
	struct super_block *sb;
	dev_t pseudo_dev;
	/* Unusable (conflicting) uuid */
	bool bad_uuid;
	/* Used as a lower layer (but maybe also as upper) */
	bool is_lower;
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
	const struct ovl_layer *layer;
	struct dentry *dentry;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	unsigned int numlayer;
	/* Number of unique fs among layers including upper fs */
	unsigned int numfs;
	const struct ovl_layer *layers;
	struct ovl_sb *fs;
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
	bool share_whiteout;
	/* Traps in ovl inode cache */
	struct inode *workbasedir_trap;
	struct inode *workdir_trap;
	struct inode *indexdir_trap;
	/* -1: disabled, 0: same fs, 1..32: number of unused ino bits */
	int xino_mode;
	/* For allocation of non-persistent inode numbers */
	atomic_long_t last_ino;
	/* Whiteout dentry cache */
	struct dentry *whiteout;
};

static inline struct vfsmount *ovl_upper_mnt(struct ovl_fs *ofs)
{
	return ofs->layers[0].mnt;
}

static inline struct ovl_fs *OVL_FS(struct super_block *sb)
{
	return (struct ovl_fs *)sb->s_fs_info;
}

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
