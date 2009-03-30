/*
  File: linux/reiserfs_xattr.h
*/

#ifndef _LINUX_REISERFS_XATTR_H
#define _LINUX_REISERFS_XATTR_H

#include <linux/types.h>

/* Magic value in header */
#define REISERFS_XATTR_MAGIC 0x52465841	/* "RFXA" */

struct reiserfs_xattr_header {
	__le32 h_magic;		/* magic number for identification */
	__le32 h_hash;		/* hash of the value */
};

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/reiserfs_fs_i.h>
#include <linux/reiserfs_fs.h>

struct inode;
struct dentry;
struct iattr;
struct super_block;
struct nameidata;

int reiserfs_xattr_register_handlers(void) __init;
void reiserfs_xattr_unregister_handlers(void);
int reiserfs_xattr_init(struct super_block *sb, int mount_flags);
int reiserfs_delete_xattrs(struct inode *inode);
int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs);

#ifdef CONFIG_REISERFS_FS_XATTR
#define has_xattr_dir(inode) (REISERFS_I(inode)->i_flags & i_has_xattr_dir)
ssize_t reiserfs_getxattr(struct dentry *dentry, const char *name,
			  void *buffer, size_t size);
int reiserfs_setxattr(struct dentry *dentry, const char *name,
		      const void *value, size_t size, int flags);
ssize_t reiserfs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int reiserfs_removexattr(struct dentry *dentry, const char *name);
int reiserfs_permission(struct inode *inode, int mask);

int reiserfs_xattr_get(struct inode *, const char *, void *, size_t);
int __reiserfs_xattr_set(struct inode *, const char *, const void *,
			 size_t, int);
int reiserfs_xattr_set(struct inode *, const char *, const void *, size_t, int);

extern struct xattr_handler reiserfs_xattr_user_handler;
extern struct xattr_handler reiserfs_xattr_trusted_handler;
extern struct xattr_handler reiserfs_xattr_security_handler;

static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
	init_rwsem(&REISERFS_I(inode)->i_xattr_sem);
}

#else

#define reiserfs_getxattr NULL
#define reiserfs_setxattr NULL
#define reiserfs_listxattr NULL
#define reiserfs_removexattr NULL

#define reiserfs_permission NULL

static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
}
#endif  /*  CONFIG_REISERFS_FS_XATTR  */

#endif  /*  __KERNEL__  */

#endif  /*  _LINUX_REISERFS_XATTR_H  */
