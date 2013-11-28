/*
 * fs/sysfs/sysfs.h - sysfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#ifndef __SYSFS_INTERNAL_H
#define __SYSFS_INTERNAL_H

#include "../kernfs/kernfs-internal.h"
#include <linux/sysfs.h>

/*
 * mount.c
 */

/*
 * Each sb is associated with one namespace tag, currently the network
 * namespace of the task which mounted this sysfs instance.  If multiple
 * tags become necessary, make the following an array and compare
 * sysfs_dirent tag against every entry.
 */
struct sysfs_super_info {
	void *ns;
};
#define sysfs_info(SB) ((struct sysfs_super_info *)(SB->s_fs_info))
extern struct sysfs_dirent sysfs_root;
extern struct kmem_cache *sysfs_dir_cachep;

/*
 * dir.c
 */
extern struct mutex sysfs_mutex;
extern spinlock_t sysfs_symlink_target_lock;
extern const struct dentry_operations sysfs_dentry_ops;

extern const struct file_operations sysfs_dir_operations;
extern const struct inode_operations sysfs_dir_inode_operations;

struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd);
void sysfs_put_active(struct sysfs_dirent *sd);
void sysfs_addrm_start(struct sysfs_addrm_cxt *acxt);
void sysfs_warn_dup(struct sysfs_dirent *parent, const char *name);
int sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd,
		  struct sysfs_dirent *parent_sd);
void sysfs_addrm_finish(struct sysfs_addrm_cxt *acxt);

struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode, int type);

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
int sysfs_inode_init(void);

/*
 * file.c
 */
extern const struct file_operations kernfs_file_operations;

int sysfs_add_file(struct sysfs_dirent *dir_sd,
		   const struct attribute *attr, bool is_bin);

int sysfs_add_file_mode_ns(struct sysfs_dirent *dir_sd,
			   const struct attribute *attr, bool is_bin,
			   umode_t amode, const void *ns);
void sysfs_unmap_bin_file(struct sysfs_dirent *sd);

/*
 * symlink.c
 */
extern const struct inode_operations sysfs_symlink_inode_operations;
int sysfs_create_link_sd(struct sysfs_dirent *sd, struct kobject *target,
			 const char *name);

#endif	/* __SYSFS_INTERNAL_H */
