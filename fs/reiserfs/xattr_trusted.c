#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <asm/uaccess.h>

#define XATTR_TRUSTED_PREFIX "trusted."

static int
trusted_get(struct inode *inode, const char *name, void *buffer, size_t size)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return reiserfs_xattr_get(inode, name, buffer, size);
}

static int
trusted_set(struct inode *inode, const char *name, const void *buffer,
	    size_t size, int flags)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return reiserfs_xattr_set(inode, name, buffer, size, flags);
}

static int trusted_del(struct inode *inode, const char *name)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return 0;
}

static int
trusted_list(struct inode *inode, const char *name, int namelen, char *out)
{
	int len = namelen;

	if (!reiserfs_xattrs(inode->i_sb))
		return 0;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return 0;

	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler trusted_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = trusted_get,
	.set = trusted_set,
	.del = trusted_del,
	.list = trusted_list,
};
