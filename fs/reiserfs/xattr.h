#include <linux/reiserfs_xattr.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rwsem.h>

struct inode;
struct dentry;
struct iattr;
struct super_block;

int reiserfs_xattr_register_handlers(void) __init;
void reiserfs_xattr_unregister_handlers(void);
int reiserfs_xattr_init(struct super_block *sb, int mount_flags);
int reiserfs_lookup_privroot(struct super_block *sb);
int reiserfs_delete_xattrs(struct inode *inode);
int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs);
int reiserfs_permission(struct inode *inode, int mask);

#ifdef CONFIG_REISERFS_FS_XATTR
#define has_xattr_dir(inode) (REISERFS_I(inode)->i_flags & i_has_xattr_dir)
ssize_t reiserfs_getxattr(struct dentry *dentry, const char *name,
			  void *buffer, size_t size);
int reiserfs_setxattr(struct dentry *dentry, const char *name,
		      const void *value, size_t size, int flags);
ssize_t reiserfs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int reiserfs_removexattr(struct dentry *dentry, const char *name);

int reiserfs_xattr_get(struct inode *, const char *, void *, size_t);
int reiserfs_xattr_set(struct inode *, const char *, const void *, size_t, int);
int reiserfs_xattr_set_handle(struct reiserfs_transaction_handle *,
			      struct inode *, const char *, const void *,
			      size_t, int);

extern const struct xattr_handler reiserfs_xattr_user_handler;
extern const struct xattr_handler reiserfs_xattr_trusted_handler;
extern const struct xattr_handler reiserfs_xattr_security_handler;
#ifdef CONFIG_REISERFS_FS_SECURITY
int reiserfs_security_init(struct inode *dir, struct inode *inode,
			   const struct qstr *qstr,
			   struct reiserfs_security_handle *sec);
int reiserfs_security_write(struct reiserfs_transaction_handle *th,
			    struct inode *inode,
			    struct reiserfs_security_handle *sec);
void reiserfs_security_free(struct reiserfs_security_handle *sec);
#endif

static inline int reiserfs_xattrs_initialized(struct super_block *sb)
{
	return REISERFS_SB(sb)->priv_root != NULL;
}

#define xattr_size(size) ((size) + sizeof(struct reiserfs_xattr_header))
static inline loff_t reiserfs_xattr_nblocks(struct inode *inode, loff_t size)
{
	loff_t ret = 0;
	if (reiserfs_file_data_log(inode)) {
		ret = _ROUND_UP(xattr_size(size), inode->i_sb->s_blocksize);
		ret >>= inode->i_sb->s_blocksize_bits;
	}
	return ret;
}

/*
 * We may have to create up to 3 objects: xattr root, xattr dir, xattr file.
 * Let's try to be smart about it.
 * xattr root: We cache it. If it's not cached, we may need to create it.
 * xattr dir: If anything has been loaded for this inode, we can set a flag
 *            saying so.
 * xattr file: Since we don't cache xattrs, we can't tell. We always include
 *             blocks for it.
 *
 * However, since root and dir can be created between calls - YOU MUST SAVE
 * THIS VALUE.
 */
static inline size_t reiserfs_xattr_jcreate_nblocks(struct inode *inode)
{
	size_t nblocks = JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);

	if ((REISERFS_I(inode)->i_flags & i_has_xattr_dir) == 0) {
		nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);
		if (d_really_is_negative(REISERFS_SB(inode->i_sb)->xattr_root))
			nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);
	}

	return nblocks;
}

static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
	init_rwsem(&REISERFS_I(inode)->i_xattr_sem);
}

#else

#define reiserfs_getxattr NULL
#define reiserfs_setxattr NULL
#define reiserfs_listxattr NULL
#define reiserfs_removexattr NULL

static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
}
#endif  /*  CONFIG_REISERFS_FS_XATTR  */

#ifndef CONFIG_REISERFS_FS_SECURITY
static inline int reiserfs_security_init(struct inode *dir,
					 struct inode *inode,
					 const struct qstr *qstr,
					 struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline int
reiserfs_security_write(struct reiserfs_transaction_handle *th,
			struct inode *inode,
			struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline void reiserfs_security_free(struct reiserfs_security_handle *sec)
{}
#endif
