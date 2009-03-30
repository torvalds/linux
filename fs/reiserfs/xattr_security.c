#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <asm/uaccess.h>

static int
security_get(struct inode *inode, const char *name, void *buffer, size_t size)
{
	if (strlen(name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	if (IS_PRIVATE(inode))
		return -EPERM;

	return reiserfs_xattr_get(inode, name, buffer, size);
}

static int
security_set(struct inode *inode, const char *name, const void *buffer,
	     size_t size, int flags)
{
	if (strlen(name) < sizeof(XATTR_SECURITY_PREFIX))
		return -EINVAL;

	if (IS_PRIVATE(inode))
		return -EPERM;

	return reiserfs_xattr_set(inode, name, buffer, size, flags);
}

static size_t security_list(struct inode *inode, char *list, size_t list_len,
			    const char *name, size_t namelen)
{
	const size_t len = namelen + 1;

	if (IS_PRIVATE(inode))
		return 0;

	if (list && len <= list_len) {
		memcpy(list, name, namelen);
		list[namelen] = '\0';
	}

	return len;
}

struct xattr_handler reiserfs_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = security_get,
	.set = security_set,
	.list = security_list,
};
