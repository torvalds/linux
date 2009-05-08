#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <linux/security.h>
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

/* Initializes the security context for a new inode and returns the number
 * of blocks needed for the transaction. If successful, reiserfs_security
 * must be released using reiserfs_security_free when the caller is done. */
int reiserfs_security_init(struct inode *dir, struct inode *inode,
			   struct reiserfs_security_handle *sec)
{
	int blocks = 0;
	int error = security_inode_init_security(inode, dir, &sec->name,
						 &sec->value, &sec->length);
	if (error) {
		if (error == -EOPNOTSUPP)
			error = 0;

		sec->name = NULL;
		sec->value = NULL;
		sec->length = 0;
		return error;
	}

	if (sec->length) {
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
	kfree(sec->name);
	kfree(sec->value);
	sec->name = NULL;
	sec->value = NULL;
}

struct xattr_handler reiserfs_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = security_get,
	.set = security_set,
	.list = security_list,
};
