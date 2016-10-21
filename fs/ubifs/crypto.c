#include "ubifs.h"

static int ubifs_crypt_get_context(struct inode *inode, void *ctx, size_t len)
{
	return ubifs_xattr_get(inode, UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT,
			       ctx, len);
}

static int ubifs_crypt_set_context(struct inode *inode, const void *ctx,
				   size_t len, void *fs_data)
{
	return ubifs_xattr_set(inode, UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT,
			       ctx, len, 0);
}

static bool ubifs_crypt_empty_dir(struct inode *inode)
{
	return ubifs_check_dir_empty(inode) == 0;
}

static unsigned int ubifs_crypt_max_namelen(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		return UBIFS_MAX_INO_DATA;
	else
		return UBIFS_MAX_NLEN;
}

static int ubifs_key_prefix(struct inode *inode, u8 **key)
{
	static char prefix[] = "ubifs:";

	*key = prefix;

	return sizeof(prefix) - 1;
}

struct fscrypt_operations ubifs_crypt_operations = {
	.flags			= FS_CFLG_INPLACE_ENCRYPTION,
	.get_context		= ubifs_crypt_get_context,
	.set_context		= ubifs_crypt_set_context,
	.is_encrypted		= __ubifs_crypt_is_encrypted,
	.empty_dir		= ubifs_crypt_empty_dir,
	.max_namelen		= ubifs_crypt_max_namelen,
	.key_prefix		= ubifs_key_prefix,
};
