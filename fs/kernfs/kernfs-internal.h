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
#include <linux/mutex.h>

#include <linux/kernfs.h>

struct sysfs_inode_attrs {
	struct iattr	ia_iattr;
	void		*ia_secdata;
	u32		ia_secdata_len;
};

#define SD_DEACTIVATED_BIAS		INT_MIN

/* SYSFS_TYPE_MASK and types are defined in include/linux/kernfs.h */

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
