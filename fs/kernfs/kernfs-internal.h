/*
 * fs/kernfs/kernfs-internal.h - kernfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#ifndef __KERNFS_INTERNAL_H
#define __KERNFS_INTERNAL_H

#include <linux/lockdep.h>
#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/mutex.h>

#include <linux/kernfs.h>

struct sysfs_open_dirent;

/* type-specific structures for sysfs_dirent->s_* union members */
struct sysfs_elem_dir {
	unsigned long		subdirs;
	/* children rbtree starts here and goes through sd->s_rb */
	struct rb_root		children;

	/*
	 * The kernfs hierarchy this directory belongs to.  This fits
	 * better directly in sysfs_dirent but is here to save space.
	 */
	struct kernfs_root	*root;
};

struct sysfs_elem_symlink {
	struct sysfs_dirent	*target_sd;
};

struct sysfs_elem_attr {
	const struct kernfs_ops	*ops;
	struct sysfs_open_dirent *open;
	loff_t			size;
};

struct sysfs_inode_attrs {
	struct iattr	ia_iattr;
	void		*ia_secdata;
	u32		ia_secdata_len;
};

/*
 * sysfs_dirent - the building block of sysfs hierarchy.  Each and
 * every sysfs node is represented by single sysfs_dirent.
 *
 * As long as s_count reference is held, the sysfs_dirent itself is
 * accessible.  Dereferencing s_elem or any other outer entity
 * requires s_active reference.
 */
struct sysfs_dirent {
	atomic_t		s_count;
	atomic_t		s_active;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
	struct sysfs_dirent	*s_parent;
	const char		*s_name;

	struct rb_node		s_rb;

	union {
		struct completion	*completion;
		struct sysfs_dirent	*removed_list;
	} u;

	const void		*s_ns; /* namespace tag */
	unsigned int		s_hash; /* ns + name hash */
	union {
		struct sysfs_elem_dir		s_dir;
		struct sysfs_elem_symlink	s_symlink;
		struct sysfs_elem_attr		s_attr;
	};

	void			*priv;

	unsigned short		s_flags;
	umode_t			s_mode;
	unsigned int		s_ino;
	struct sysfs_inode_attrs *s_iattr;
};

#define SD_DEACTIVATED_BIAS		INT_MIN

#define SYSFS_TYPE_MASK			0x000f
#define SYSFS_DIR			0x0001
#define SYSFS_KOBJ_ATTR			0x0002
#define SYSFS_KOBJ_LINK			0x0004
#define SYSFS_COPY_NAME			(SYSFS_DIR | SYSFS_KOBJ_LINK)
#define SYSFS_ACTIVE_REF		SYSFS_KOBJ_ATTR

#define SYSFS_FLAG_MASK			~SYSFS_TYPE_MASK
#define SYSFS_FLAG_REMOVED		0x0010
#define SYSFS_FLAG_NS			0x0020
#define SYSFS_FLAG_HAS_SEQ_SHOW		0x0040
#define SYSFS_FLAG_HAS_MMAP		0x0080
#define SYSFS_FLAG_LOCKDEP		0x0100

static inline unsigned int sysfs_type(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_TYPE_MASK;
}

/**
 * kernfs_root - find out the kernfs_root a sysfs_dirent belongs to
 * @sd: sysfs_dirent of interest
 *
 * Return the kernfs_root @sd belongs to.
 */
static inline struct kernfs_root *kernfs_root(struct sysfs_dirent *sd)
{
	/* if parent exists, it's always a dir; otherwise, @sd is a dir */
	if (sd->s_parent)
		sd = sd->s_parent;
	return sd->s_dir.root;
}

/*
 * Context structure to be used while adding/removing nodes.
 */
struct sysfs_addrm_cxt {
	struct sysfs_dirent	*removed;
};

#include "../sysfs/sysfs.h"

/*
 * mount.c
 */
struct sysfs_super_info {
	/*
	 * The root associated with this super_block.  Each super_block is
	 * identified by the root and ns it's associated with.
	 */
	struct kernfs_root	*root;

	/*
	 * Each sb is associated with one namespace tag, currently the network
	 * namespace of the task which mounted this sysfs instance.  If multiple
	 * tags become necessary, make the following an array and compare
	 * sysfs_dirent tag against every entry.
	 */
	const void		*ns;
};
#define sysfs_info(SB) ((struct sysfs_super_info *)(SB->s_fs_info))

extern struct kmem_cache *sysfs_dir_cachep;

/*
 * inode.c
 */
struct inode *sysfs_get_inode(struct super_block *sb, struct sysfs_dirent *sd);
void sysfs_evict_inode(struct inode *inode);
int sysfs_permission(struct inode *inode, int mask);
int sysfs_setattr(struct dentry *dentry, struct iattr *iattr);
int sysfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat);
int sysfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		   size_t size, int flags);
void sysfs_inode_init(void);

/*
 * dir.c
 */
extern struct mutex sysfs_mutex;
extern const struct dentry_operations sysfs_dentry_ops;
extern const struct file_operations sysfs_dir_operations;
extern const struct inode_operations sysfs_dir_inode_operations;

struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd);
void sysfs_put_active(struct sysfs_dirent *sd);
void sysfs_addrm_start(struct sysfs_addrm_cxt *acxt);
int sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd,
		  struct sysfs_dirent *parent_sd);
void sysfs_addrm_finish(struct sysfs_addrm_cxt *acxt);
struct sysfs_dirent *sysfs_new_dirent(struct kernfs_root *root,
				      const char *name, umode_t mode, int type);

/*
 * file.c
 */
extern const struct file_operations kernfs_file_operations;

void sysfs_unmap_bin_file(struct sysfs_dirent *sd);

/*
 * symlink.c
 */
extern const struct inode_operations sysfs_symlink_inode_operations;

#endif	/* __KERNFS_INTERNAL_H */
