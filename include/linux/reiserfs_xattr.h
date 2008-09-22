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

struct reiserfs_xattr_handler {
	char *prefix;
	int (*init) (void);
	void (*exit) (void);
	int (*get) (struct inode * inode, const char *name, void *buffer,
		    size_t size);
	int (*set) (struct inode * inode, const char *name, const void *buffer,
		    size_t size, int flags);
	int (*del) (struct inode * inode, const char *name);
	int (*list) (struct inode * inode, const char *name, int namelen,
		     char *out);
	struct list_head handlers;
};

#ifdef CONFIG_REISERFS_FS_XATTR
#define is_reiserfs_priv_object(inode) IS_PRIVATE(inode)
#define has_xattr_dir(inode) (REISERFS_I(inode)->i_flags & i_has_xattr_dir)
ssize_t reiserfs_getxattr(struct dentry *dentry, const char *name,
			  void *buffer, size_t size);
int reiserfs_setxattr(struct dentry *dentry, const char *name,
		      const void *value, size_t size, int flags);
ssize_t reiserfs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int reiserfs_removexattr(struct dentry *dentry, const char *name);
int reiserfs_delete_xattrs(struct inode *inode);
int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs);
int reiserfs_xattr_init(struct super_block *sb, int mount_flags);
int reiserfs_permission(struct inode *inode, int mask);

int reiserfs_xattr_del(struct inode *, const char *);
int reiserfs_xattr_get(const struct inode *, const char *, void *, size_t);
int reiserfs_xattr_set(struct inode *, const char *, const void *, size_t, int);

extern struct reiserfs_xattr_handler user_handler;
extern struct reiserfs_xattr_handler trusted_handler;
extern struct reiserfs_xattr_handler security_handler;

int reiserfs_xattr_register_handlers(void) __init;
void reiserfs_xattr_unregister_handlers(void);

static inline void reiserfs_write_lock_xattrs(struct super_block *sb)
{
	down_write(&REISERFS_XATTR_DIR_SEM(sb));
}
static inline void reiserfs_write_unlock_xattrs(struct super_block *sb)
{
	up_write(&REISERFS_XATTR_DIR_SEM(sb));
}
static inline void reiserfs_read_lock_xattrs(struct super_block *sb)
{
	down_read(&REISERFS_XATTR_DIR_SEM(sb));
}

static inline void reiserfs_read_unlock_xattrs(struct super_block *sb)
{
	up_read(&REISERFS_XATTR_DIR_SEM(sb));
}

static inline void reiserfs_write_lock_xattr_i(struct inode *inode)
{
	down_write(&REISERFS_I(inode)->xattr_sem);
}
static inline void reiserfs_write_unlock_xattr_i(struct inode *inode)
{
	up_write(&REISERFS_I(inode)->xattr_sem);
}
static inline void reiserfs_read_lock_xattr_i(struct inode *inode)
{
	down_read(&REISERFS_I(inode)->xattr_sem);
}

static inline void reiserfs_read_unlock_xattr_i(struct inode *inode)
{
	up_read(&REISERFS_I(inode)->xattr_sem);
}

static inline void reiserfs_mark_inode_private(struct inode *inode)
{
	inode->i_flags |= S_PRIVATE;
}

static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
	init_rwsem(&REISERFS_I(inode)->xattr_sem);
}

#else

#define is_reiserfs_priv_object(inode) 0
#define reiserfs_mark_inode_private(inode) do {;} while(0)
#define reiserfs_getxattr NULL
#define reiserfs_setxattr NULL
#define reiserfs_listxattr NULL
#define reiserfs_removexattr NULL
#define reiserfs_write_lock_xattrs(sb) do {;} while(0)
#define reiserfs_write_unlock_xattrs(sb) do {;} while(0)
#define reiserfs_read_lock_xattrs(sb)
#define reiserfs_read_unlock_xattrs(sb)

#define reiserfs_permission NULL

#define reiserfs_xattr_register_handlers() 0
#define reiserfs_xattr_unregister_handlers()

static inline int reiserfs_delete_xattrs(struct inode *inode)
{
	return 0;
};
static inline int reiserfs_chown_xattrs(struct inode *inode,
					struct iattr *attrs)
{
	return 0;
};
static inline int reiserfs_xattr_init(struct super_block *sb, int mount_flags)
{
	sb->s_flags = (sb->s_flags & ~MS_POSIXACL);	/* to be sure */
	return 0;
};
static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
}
#endif  /*  CONFIG_REISERFS_FS_XATTR  */

#endif  /*  __KERNEL__  */

#endif  /*  _LINUX_REISERFS_XATTR_H  */
