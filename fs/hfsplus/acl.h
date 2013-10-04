/*
 * linux/fs/hfsplus/acl.h
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for Posix Access Control Lists (ACLs) support.
 */

#include <linux/posix_acl_xattr.h>

#ifdef CONFIG_HFSPLUS_FS_POSIX_ACL

/* posix_acl.c */
struct posix_acl *hfsplus_get_posix_acl(struct inode *inode, int type);
extern int hfsplus_posix_acl_chmod(struct inode *);
extern int hfsplus_init_posix_acl(struct inode *, struct inode *);

#else  /* CONFIG_HFSPLUS_FS_POSIX_ACL */
#define hfsplus_get_posix_acl NULL

static inline int hfsplus_posix_acl_chmod(struct inode *inode)
{
	return 0;
}

static inline int hfsplus_init_posix_acl(struct inode *inode, struct inode *dir)
{
	return 0;
}
#endif  /* CONFIG_HFSPLUS_FS_POSIX_ACL */
