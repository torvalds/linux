// SPDX-License-Identifier: GPL-2.0
#include "reiserfs.h"
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/slab.h>
#include "xattr.h"
#include <linux/security.h>
#include <linux/uaccess.h>

static int
security_get(const struct xattr_handler *handler, struct dentry *unused,
	     struct inode *inode, const char *name, void *buffer, size_t size,
	     int flags)
{
	if (IS_PRIVATE(inode))
		return -EPERM;

	return reiserfs_xattr_get(inode, xattr_full_name(handler, name),
				  buffer, size);
}

static int
security_set(const struct xattr_handler *handler, struct dentry *unused,
	     struct inode *inode, const char *name, const void *buffer,
	     size_t size, int flags)
{
	if (IS_PRIVATE(inode))
		return -EPERM;

	return reiserfs_xattr_set(inode,
				  xattr_full_name(handler, name),
				  buffer, size, flags);
}

static bool security_list(struct dentry *dentry)
{
	return !IS_PRIVATE(d_inode(dentry));
}

/* Initializes the security context for a new inode and returns the number
 * of blocks needed for the transaction. If successful, reiserfs_security
 * must be released using reiserfs_security_free when the caller is done. */
int reiserfs_security_init(struct inode *dir, struct inode *inode,
			   const struct qstr *qstr,
			   struct reiserfs_security_handle *sec)
{
	int blocks = 0;
	int error;

	sec->name = NULL;
	sec->value = NULL;

	/* Don't add selinux attributes on xattrs - they'll never get used */
	if (IS_PRIVATE(dir))
		return 0;

	error = security_old_inode_init_security(inode, dir, qstr, &sec->name,
						 &sec->value, &sec->length);
	if (error) {
		if (error == -EOPNOTSUPP)
			error = 0;

		sec->name = NULL;
		sec->value = NULL;
		sec->length = 0;
		return error;
	}

	if (sec->length && reiserfs_xattrs_initialized(inode->i_sb)) {
		blocks = reiserfs_xattr_jcreate_nblocks(inode) +
			 reiserfs_xattr_nblocks(inode, sec->length);
		/* We don't want to count the directories twice if we have
		 * a default ACL. */
		REISERFS_I(inode)->i_flags |= i_has_xattr_dir;
	}
	return blocks;
}

int reiserfs_security_write(struct reiserfs_transaction_handle *th,
			    struct inode *inode,
			    struct reiserfs_security_handle *sec)
{
	int error;
	if (strlen(sec->name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	error = reiserfs_xattr_set_handle(th, inode, sec->name, sec->value,
					  sec->length, XATTR_CREATE);
	if (error == -ENODATA || error == -EOPNOTSUPP)
		error = 0;

	return error;
}

void reiserfs_security_free(struct reiserfs_security_handle *sec)
{
	kfree(sec->value);
	sec->name = NULL;
	sec->value = NULL;
}

const struct xattr_handler reiserfs_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = security_get,
	.set = security_set,
	.list = security_list,
};
