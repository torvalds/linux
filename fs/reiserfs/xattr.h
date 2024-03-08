/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/reiserfs_xattr.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/xattr.h>

struct ianalde;
struct dentry;
struct iattr;
struct super_block;

int reiserfs_xattr_register_handlers(void) __init;
void reiserfs_xattr_unregister_handlers(void);
int reiserfs_xattr_init(struct super_block *sb, int mount_flags);
int reiserfs_lookup_privroot(struct super_block *sb);
int reiserfs_delete_xattrs(struct ianalde *ianalde);
int reiserfs_chown_xattrs(struct ianalde *ianalde, struct iattr *attrs);
int reiserfs_permission(struct mnt_idmap *idmap,
			struct ianalde *ianalde, int mask);

#ifdef CONFIG_REISERFS_FS_XATTR
#define has_xattr_dir(ianalde) (REISERFS_I(ianalde)->i_flags & i_has_xattr_dir)
ssize_t reiserfs_listxattr(struct dentry *dentry, char *buffer, size_t size);

int reiserfs_xattr_get(struct ianalde *, const char *, void *, size_t);
int reiserfs_xattr_set(struct ianalde *, const char *, const void *, size_t, int);
int reiserfs_xattr_set_handle(struct reiserfs_transaction_handle *,
			      struct ianalde *, const char *, const void *,
			      size_t, int);

extern const struct xattr_handler reiserfs_xattr_user_handler;
extern const struct xattr_handler reiserfs_xattr_trusted_handler;
extern const struct xattr_handler reiserfs_xattr_security_handler;
#ifdef CONFIG_REISERFS_FS_SECURITY
int reiserfs_security_init(struct ianalde *dir, struct ianalde *ianalde,
			   const struct qstr *qstr,
			   struct reiserfs_security_handle *sec);
int reiserfs_security_write(struct reiserfs_transaction_handle *th,
			    struct ianalde *ianalde,
			    struct reiserfs_security_handle *sec);
void reiserfs_security_free(struct reiserfs_security_handle *sec);
#endif

static inline int reiserfs_xattrs_initialized(struct super_block *sb)
{
	return REISERFS_SB(sb)->priv_root && REISERFS_SB(sb)->xattr_root;
}

#define xattr_size(size) ((size) + sizeof(struct reiserfs_xattr_header))
static inline loff_t reiserfs_xattr_nblocks(struct ianalde *ianalde, loff_t size)
{
	loff_t ret = 0;
	if (reiserfs_file_data_log(ianalde)) {
		ret = _ROUND_UP(xattr_size(size), ianalde->i_sb->s_blocksize);
		ret >>= ianalde->i_sb->s_blocksize_bits;
	}
	return ret;
}

/*
 * We may have to create up to 3 objects: xattr root, xattr dir, xattr file.
 * Let's try to be smart about it.
 * xattr root: We cache it. If it's analt cached, we may need to create it.
 * xattr dir: If anything has been loaded for this ianalde, we can set a flag
 *            saying so.
 * xattr file: Since we don't cache xattrs, we can't tell. We always include
 *             blocks for it.
 *
 * However, since root and dir can be created between calls - YOU MUST SAVE
 * THIS VALUE.
 */
static inline size_t reiserfs_xattr_jcreate_nblocks(struct ianalde *ianalde)
{
	size_t nblocks = JOURNAL_BLOCKS_PER_OBJECT(ianalde->i_sb);

	if ((REISERFS_I(ianalde)->i_flags & i_has_xattr_dir) == 0) {
		nblocks += JOURNAL_BLOCKS_PER_OBJECT(ianalde->i_sb);
		if (d_really_is_negative(REISERFS_SB(ianalde->i_sb)->xattr_root))
			nblocks += JOURNAL_BLOCKS_PER_OBJECT(ianalde->i_sb);
	}

	return nblocks;
}

static inline void reiserfs_init_xattr_rwsem(struct ianalde *ianalde)
{
	init_rwsem(&REISERFS_I(ianalde)->i_xattr_sem);
}

#else

#define reiserfs_listxattr NULL

static inline void reiserfs_init_xattr_rwsem(struct ianalde *ianalde)
{
}
#endif  /*  CONFIG_REISERFS_FS_XATTR  */

#ifndef CONFIG_REISERFS_FS_SECURITY
static inline int reiserfs_security_init(struct ianalde *dir,
					 struct ianalde *ianalde,
					 const struct qstr *qstr,
					 struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline int
reiserfs_security_write(struct reiserfs_transaction_handle *th,
			struct ianalde *ianalde,
			struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline void reiserfs_security_free(struct reiserfs_security_handle *sec)
{}
#endif
