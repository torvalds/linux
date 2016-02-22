/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>

struct ovl_entry;

enum ovl_path_type {
	__OVL_PATH_PURE		= (1 << 0),
	__OVL_PATH_UPPER	= (1 << 1),
	__OVL_PATH_MERGE	= (1 << 2),
};

#define OVL_TYPE_UPPER(type)	((type) & __OVL_PATH_UPPER)
#define OVL_TYPE_MERGE(type)	((type) & __OVL_PATH_MERGE)
#define OVL_TYPE_PURE_UPPER(type) ((type) & __OVL_PATH_PURE)
#define OVL_TYPE_MERGE_OR_LOWER(type) \
	(OVL_TYPE_MERGE(type) || !OVL_TYPE_UPPER(type))

#define OVL_XATTR_PRE_NAME "trusted.overlay."
#define OVL_XATTR_PRE_LEN  16
#define OVL_XATTR_OPAQUE   OVL_XATTR_PRE_NAME"opaque"

static inline int ovl_do_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_rmdir(dir, dentry);
	pr_debug("rmdir(%pd2) = %i\n", dentry, err);
	return err;
}

static inline int ovl_do_unlink(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_unlink(dir, dentry, NULL);
	pr_debug("unlink(%pd2) = %i\n", dentry, err);
	return err;
}

static inline int ovl_do_link(struct dentry *old_dentry, struct inode *dir,
			      struct dentry *new_dentry, bool debug)
{
	int err = vfs_link(old_dentry, dir, new_dentry, NULL);
	if (debug) {
		pr_debug("link(%pd2, %pd2) = %i\n",
			 old_dentry, new_dentry, err);
	}
	return err;
}

static inline int ovl_do_create(struct inode *dir, struct dentry *dentry,
			     umode_t mode, bool debug)
{
	int err = vfs_create(dir, dentry, mode, true);
	if (debug)
		pr_debug("create(%pd2, 0%o) = %i\n", dentry, mode, err);
	return err;
}

static inline int ovl_do_mkdir(struct inode *dir, struct dentry *dentry,
			       umode_t mode, bool debug)
{
	int err = vfs_mkdir(dir, dentry, mode);
	if (debug)
		pr_debug("mkdir(%pd2, 0%o) = %i\n", dentry, mode, err);
	return err;
}

static inline int ovl_do_mknod(struct inode *dir, struct dentry *dentry,
			       umode_t mode, dev_t dev, bool debug)
{
	int err = vfs_mknod(dir, dentry, mode, dev);
	if (debug) {
		pr_debug("mknod(%pd2, 0%o, 0%o) = %i\n",
			 dentry, mode, dev, err);
	}
	return err;
}

static inline int ovl_do_symlink(struct inode *dir, struct dentry *dentry,
				 const char *oldname, bool debug)
{
	int err = vfs_symlink(dir, dentry, oldname);
	if (debug)
		pr_debug("symlink(\"%s\", %pd2) = %i\n", oldname, dentry, err);
	return err;
}

static inline int ovl_do_setxattr(struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	int err = vfs_setxattr(dentry, name, value, size, flags);
	pr_debug("setxattr(%pd2, \"%s\", \"%*s\", 0x%x) = %i\n",
		 dentry, name, (int) size, (char *) value, flags, err);
	return err;
}

static inline int ovl_do_removexattr(struct dentry *dentry, const char *name)
{
	int err = vfs_removexattr(dentry, name);
	pr_debug("removexattr(%pd2, \"%s\") = %i\n", dentry, name, err);
	return err;
}

static inline int ovl_do_rename(struct inode *olddir, struct dentry *olddentry,
				struct inode *newdir, struct dentry *newdentry,
				unsigned int flags)
{
	int err;

	pr_debug("rename2(%pd2, %pd2, 0x%x)\n",
		 olddentry, newdentry, flags);

	err = vfs_rename(olddir, olddentry, newdir, newdentry, NULL, flags);

	if (err) {
		pr_debug("...rename2(%pd2, %pd2, ...) = %i\n",
			 olddentry, newdentry, err);
	}
	return err;
}

static inline int ovl_do_whiteout(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_whiteout(dir, dentry);
	pr_debug("whiteout(%pd2) = %i\n", dentry, err);
	return err;
}

enum ovl_path_type ovl_path_type(struct dentry *dentry);
u64 ovl_dentry_version_get(struct dentry *dentry);
void ovl_dentry_version_inc(struct dentry *dentry);
void ovl_path_upper(struct dentry *dentry, struct path *path);
void ovl_path_lower(struct dentry *dentry, struct path *path);
enum ovl_path_type ovl_path_real(struct dentry *dentry, struct path *path);
int ovl_path_next(int idx, struct dentry *dentry, struct path *path);
struct dentry *ovl_dentry_upper(struct dentry *dentry);
struct dentry *ovl_dentry_lower(struct dentry *dentry);
struct dentry *ovl_dentry_real(struct dentry *dentry);
struct dentry *ovl_entry_real(struct ovl_entry *oe, bool *is_upper);
struct vfsmount *ovl_entry_mnt_real(struct ovl_entry *oe, struct inode *inode,
				    bool is_upper);
struct ovl_dir_cache *ovl_dir_cache(struct dentry *dentry);
bool ovl_is_default_permissions(struct inode *inode);
void ovl_set_dir_cache(struct dentry *dentry, struct ovl_dir_cache *cache);
struct dentry *ovl_workdir(struct dentry *dentry);
int ovl_want_write(struct dentry *dentry);
void ovl_drop_write(struct dentry *dentry);
bool ovl_dentry_is_opaque(struct dentry *dentry);
void ovl_dentry_set_opaque(struct dentry *dentry, bool opaque);
bool ovl_is_whiteout(struct dentry *dentry);
void ovl_dentry_update(struct dentry *dentry, struct dentry *upperdentry);
struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags);
struct file *ovl_path_open(struct path *path, int flags);

struct dentry *ovl_upper_create(struct dentry *upperdir, struct dentry *dentry,
				struct kstat *stat, const char *link);

/* readdir.c */
extern const struct file_operations ovl_dir_operations;
int ovl_check_empty_dir(struct dentry *dentry, struct list_head *list);
void ovl_cleanup_whiteouts(struct dentry *upper, struct list_head *list);
void ovl_cache_free(struct list_head *list);

/* inode.c */
int ovl_setattr(struct dentry *dentry, struct iattr *attr);
int ovl_permission(struct inode *inode, int mask);
int ovl_setxattr(struct dentry *dentry, const char *name,
		 const void *value, size_t size, int flags);
ssize_t ovl_getxattr(struct dentry *dentry, const char *name,
		     void *value, size_t size);
ssize_t ovl_listxattr(struct dentry *dentry, char *list, size_t size);
int ovl_removexattr(struct dentry *dentry, const char *name);
struct inode *ovl_d_select_inode(struct dentry *dentry, unsigned file_flags);

struct inode *ovl_new_inode(struct super_block *sb, umode_t mode,
			    struct ovl_entry *oe);
static inline void ovl_copyattr(struct inode *from, struct inode *to)
{
	to->i_uid = from->i_uid;
	to->i_gid = from->i_gid;
}

/* dir.c */
extern const struct inode_operations ovl_dir_inode_operations;
struct dentry *ovl_lookup_temp(struct dentry *workdir, struct dentry *dentry);
int ovl_create_real(struct inode *dir, struct dentry *newdentry,
		    struct kstat *stat, const char *link,
		    struct dentry *hardlink, bool debug);
void ovl_cleanup(struct inode *dir, struct dentry *dentry);

/* copy_up.c */
int ovl_copy_up(struct dentry *dentry);
int ovl_copy_up_one(struct dentry *parent, struct dentry *dentry,
		    struct path *lowerpath, struct kstat *stat);
int ovl_copy_xattr(struct dentry *old, struct dentry *new);
int ovl_set_attr(struct dentry *upper, struct kstat *stat);
