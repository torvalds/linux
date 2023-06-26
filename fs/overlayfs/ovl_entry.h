/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 */

struct ovl_config {
	char *upperdir;
	char *workdir;
	bool default_permissions;
	int redirect_mode;
	int verity_mode;
	bool index;
	int uuid;
	bool nfs_export;
	int xino;
	bool metacopy;
	bool userxattr;
	bool ovl_volatile;
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
	/* ovl_free_fs() relies on @mnt being the first member! */
	struct vfsmount *mnt;
	/* Trap in ovl inode cache */
	struct inode *trap;
	struct ovl_sb *fs;
	/* Index of this layer in fs root (upper idx == 0) */
	int idx;
	/* One fsid per unique underlying sb (upper fsid == 0) */
	int fsid;
	char *name;
};

/*
 * ovl_free_fs() relies on @mnt being the first member when unmounting
 * the private mounts created for each layer. Let's check both the
 * offset and type.
 */
static_assert(offsetof(struct ovl_layer, mnt) == 0);
static_assert(__same_type(typeof_member(struct ovl_layer, mnt), struct vfsmount *));

struct ovl_path {
	const struct ovl_layer *layer;
	struct dentry *dentry;
};

struct ovl_entry {
	unsigned int __numlower;
	struct ovl_path __lowerstack[];
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	unsigned int numlayer;
	/* Number of unique fs among layers including upper fs */
	unsigned int numfs;
	/* Number of data-only lower layers */
	unsigned int numdatalayer;
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
	bool nofh;
	/* Did we take the inuse lock? */
	bool upperdir_locked;
	bool workdir_locked;
	/* Traps in ovl inode cache */
	struct inode *workbasedir_trap;
	struct inode *workdir_trap;
	struct inode *indexdir_trap;
	/* -1: disabled, 0: same fs, 1..32: number of unused ino bits */
	int xino_mode;
	/* For allocation of non-persistent inode numbers */
	atomic_long_t last_ino;
	/* Shared whiteout cache */
	struct dentry *whiteout;
	bool no_shared_whiteout;
	/* r/o snapshot of upperdir sb's only taken on volatile mounts */
	errseq_t errseq;
};

/* Number of lower layers, not including data-only layers */
static inline unsigned int ovl_numlowerlayer(struct ovl_fs *ofs)
{
	return ofs->numlayer - ofs->numdatalayer - 1;
}

static inline struct vfsmount *ovl_upper_mnt(struct ovl_fs *ofs)
{
	return ofs->layers[0].mnt;
}

static inline struct mnt_idmap *ovl_upper_mnt_idmap(struct ovl_fs *ofs)
{
	return mnt_idmap(ovl_upper_mnt(ofs));
}

static inline struct ovl_fs *OVL_FS(struct super_block *sb)
{
	return (struct ovl_fs *)sb->s_fs_info;
}

static inline bool ovl_should_sync(struct ovl_fs *ofs)
{
	return !ofs->config.ovl_volatile;
}

static inline unsigned int ovl_numlower(struct ovl_entry *oe)
{
	return oe ? oe->__numlower : 0;
}

static inline struct ovl_path *ovl_lowerstack(struct ovl_entry *oe)
{
	return ovl_numlower(oe) ? oe->__lowerstack : NULL;
}

static inline struct ovl_path *ovl_lowerpath(struct ovl_entry *oe)
{
	return ovl_lowerstack(oe);
}

static inline struct ovl_path *ovl_lowerdata(struct ovl_entry *oe)
{
	struct ovl_path *lowerstack = ovl_lowerstack(oe);

	return lowerstack ? &lowerstack[oe->__numlower - 1] : NULL;
}

/* May return NULL if lazy lookup of lowerdata is needed */
static inline struct dentry *ovl_lowerdata_dentry(struct ovl_entry *oe)
{
	struct ovl_path *lowerdata = ovl_lowerdata(oe);

	return lowerdata ? READ_ONCE(lowerdata->dentry) : NULL;
}

/* private information held for every overlayfs dentry */
static inline unsigned long *OVL_E_FLAGS(struct dentry *dentry)
{
	return (unsigned long *) &dentry->d_fsdata;
}

struct ovl_inode {
	union {
		struct ovl_dir_cache *cache;	/* directory */
		const char *lowerdata_redirect;	/* regular file */
	};
	const char *redirect;
	u64 version;
	unsigned long flags;
	struct inode vfs_inode;
	struct dentry *__upperdentry;
	struct ovl_entry *oe;

	/* synchronize copy up and more */
	struct mutex lock;
};

static inline struct ovl_inode *OVL_I(struct inode *inode)
{
	return container_of(inode, struct ovl_inode, vfs_inode);
}

static inline struct ovl_entry *OVL_I_E(struct inode *inode)
{
	return inode ? OVL_I(inode)->oe : NULL;
}

static inline struct ovl_entry *OVL_E(struct dentry *dentry)
{
	return OVL_I_E(d_inode(dentry));
}

static inline struct dentry *ovl_upperdentry_dereference(struct ovl_inode *oi)
{
	return READ_ONCE(oi->__upperdentry);
}
