#ifndef LINUX_GENERIC_ACL_H
#define LINUX_GENERIC_ACL_H

#include <linux/xattr.h>

struct inode;

extern struct xattr_handler generic_acl_access_handler;
extern struct xattr_handler generic_acl_default_handler;

int generic_acl_init(struct inode *, struct inode *);
int generic_acl_chmod(struct inode *);
int generic_check_acl(struct inode *inode, int mask);

#endif /* LINUX_GENERIC_ACL_H */
