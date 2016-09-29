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
#include <linux/xattr.h>

#include <linux/kernfs.h>

struct kernfs_iattrs {
	struct iattr		ia_iattr;
	void			*ia_secdata;
	u32			ia_secdata_len;

	struct simple_xattrs	xattrs;
};

/* +1 to avoid triggering overflow warning when negating it */
#define KN_DEACTIVATED_BIAS		(INT_MIN + 1)

/* KERNFS_TYPE_MASK and types are defined in include/linux/kernfs.h */

/**
 * kernfs_root - find out the kernfs_root a kernfs_node belongs to
 * @kn: kernfs_node of interest
 *
 * Return the kernfs_root @kn belongs to.
 */
static inline struct kernfs_root *kernfs_root(struct kernfs_node *kn)
{
	/* if parent exists, it's always a dir; otherwise, @sd is a dir */
	if (kn->parent)
		kn = kn->parent;
	return kn->dir.root;
}

/*
 * mount.c
 */
struct kernfs_super_info {
	struct super_block	*sb;

	/*
	 * The root associated with this super_block.  Each super_block is
	 * identified by the root and ns it's associated with.
	 */
	struct kernfs_root	*root;

	/*
	 * Each sb is associated with one namespace tag, currently the
	 * network namespace of the task which mounted this kernfs
	 * instance.  If multiple tags become necessary, make the following
	 * an array and compare kernfs_node tag against every entry.
	 */
	const void		*ns;

	/* anchored at kernfs_root->supers, protected by kernfs_mutex */
	struct list_head	node;
};
#define kernfs_info(SB) ((struct kernfs_super_info *)(SB->s_fs_info))

extern const struct super_operations kernfs_sops;
extern struct kmem_cache *kernfs_node_cache;

/*
 * inode.c
 */
extern const struct xattr_handler *kernfs_xattr_handlers[];
void kernfs_evict_inode(struct inode *inode);
int kernfs_iop_permission(struct inode *inode, int mask);
int kernfs_iop_setattr(struct dentry *dentry, struct iattr *iattr);
int kernfs_iop_getattr(struct vfsmount *mnt, struct dentry *dentry,
		       struct kstat *stat);
ssize_t kernfs_iop_listxattr(struct dentry *dentry, char *buf, size_t size);

/*
 * dir.c
 */
extern struct mutex kernfs_mutex;
extern const struct dentry_operations kernfs_dops;
extern const struct file_operations kernfs_dir_fops;
extern const struct inode_operations kernfs_dir_iops;

struct kernfs_node *kernfs_get_active(struct kernfs_node *kn);
void kernfs_put_active(struct kernfs_node *kn);
int kernfs_add_one(struct kernfs_node *kn);
struct kernfs_node *kernfs_new_node(struct kernfs_node *parent,
				    const char *name, umode_t mode,
				    unsigned flags);

/*
 * file.c
 */
extern const struct file_operations kernfs_file_fops;

void kernfs_unmap_bin_file(struct kernfs_node *kn);

/*
 * symlink.c
 */
extern const struct inode_operations kernfs_symlink_iops;

#endif	/* __KERNFS_INTERNAL_H */
