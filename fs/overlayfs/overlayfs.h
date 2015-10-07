/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

struct ovl_entry;

enum ovl_path_type {
	OVL_PATH_UPPER,
	OVL_PATH_MERGE,
	OVL_PATH_LOWER,
};

extern const char *ovl_opaque_xattr;
extern const char *ovl_whiteout_xattr;
extern const struct dentry_operations ovl_dentry_operations;

enum ovl_path_type ovl_path_type(struct dentry *dentry);
u64 ovl_dentry_version_get(struct dentry *dentry);
void ovl_dentry_version_inc(struct dentry *dentry);
void ovl_path_upper(struct dentry *dentry, struct path *path);
void ovl_path_lower(struct dentry *dentry, struct path *path);
enum ovl_path_type ovl_path_real(struct dentry *dentry, struct path *path);
struct dentry *ovl_dentry_upper(struct dentry *dentry);
struct dentry *ovl_dentry_lower(struct dentry *dentry);
struct dentry *ovl_dentry_real(struct dentry *dentry);
struct dentry *ovl_entry_real(struct ovl_entry *oe, bool *is_upper);
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
int ovl_check_empty_and_clear(struct dentry *dentry, enum ovl_path_type type);

/* inode.c */
int ovl_setattr(struct dentry *dentry, struct iattr *attr);
int ovl_permission(struct inode *inode, int mask);
int ovl_setxattr(struct dentry *dentry, const char *name,
		 const void *value, size_t size, int flags);
ssize_t ovl_getxattr(struct dentry *dentry, const char *name,
		     void *value, size_t size);
ssize_t ovl_listxattr(struct dentry *dentry, char *list, size_t size);
int ovl_removexattr(struct dentry *dentry, const char *name);

struct inode *ovl_new_inode(struct super_block *sb, umode_t mode,
			    struct ovl_entry *oe);
static inline void ovl_copyattr(struct inode *from, struct inode *to)
{
	to->i_uid = from->i_uid;
	to->i_gid = from->i_gid;
}

/* dir.c */
extern const struct inode_operations ovl_dir_inode_operations;

/* copy_up.c */
int ovl_copy_up(struct dentry *dentry);
int ovl_copy_up_truncate(struct dentry *dentry, loff_t size);
