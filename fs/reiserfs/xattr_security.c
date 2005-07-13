#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <asm/uaccess.h>

#define XATTR_SECURITY_PREFIX "security."

static int
security_get(struct inode *inode, const char *name, void *buffer, size_t size)
{
	if (strlen(name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	if (is_reiserfs_priv_object(inode))
		return -EPERM;

	return reiserfs_xattr_get(inode, name, buffer, size);
}

static int
security_set(struct inode *inode, const char *name, const void *buffer,
	     size_t size, int flags)
{
	if (strlen(name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	if (is_reiserfs_priv_object(inode))
		return -EPERM;

	return reiserfs_xattr_set(inode, name, buffer, size, flags);
}

static int security_del(struct inode *inode, const char *name)
{
	if (strlen(name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	if (is_reiserfs_priv_object(inode))
		return -EPERM;

	return 0;
}

static int
security_list(struct inode *inode, const char *name, int namelen, char *out)
{
	int len = namelen;

	if (is_reiserfs_priv_object(inode))
		return 0;

	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = security_get,
	.set = security_set,
	.del = security_del,
	.list = security_list,
};
