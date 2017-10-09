/*
 * fs/crypto/hooks.c
 *
 * Encryption hooks for higher-level filesystem operations.
 */

#include <linux/ratelimit.h>
#include "fscrypt_private.h"

/**
 * fscrypt_file_open - prepare to open a possibly-encrypted regular file
 * @inode: the inode being opened
 * @filp: the struct file being set up
 *
 * Currently, an encrypted regular file can only be opened if its encryption key
 * is available; access to the raw encrypted contents is not supported.
 * Therefore, we first set up the inode's encryption key (if not already done)
 * and return an error if it's unavailable.
 *
 * We also verify that if the parent directory (from the path via which the file
 * is being opened) is encrypted, then the inode being opened uses the same
 * encryption policy.  This is needed as part of the enforcement that all files
 * in an encrypted directory tree use the same encryption policy, as a
 * protection against certain types of offline attacks.  Note that this check is
 * needed even when opening an *unencrypted* file, since it's forbidden to have
 * an unencrypted file in an encrypted directory.
 *
 * Return: 0 on success, -ENOKEY if the key is missing, or another -errno code
 */
int fscrypt_file_open(struct inode *inode, struct file *filp)
{
	int err;
	struct dentry *dir;

	err = fscrypt_require_key(inode);
	if (err)
		return err;

	dir = dget_parent(file_dentry(filp));
	if (IS_ENCRYPTED(d_inode(dir)) &&
	    !fscrypt_has_permitted_context(d_inode(dir), inode)) {
		pr_warn_ratelimited("fscrypt: inconsistent encryption contexts: %lu/%lu",
				    d_inode(dir)->i_ino, inode->i_ino);
		err = -EPERM;
	}
	dput(dir);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_file_open);
