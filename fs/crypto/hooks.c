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

int __fscrypt_prepare_link(struct inode *inode, struct inode *dir)
{
	int err;

	err = fscrypt_require_key(dir);
	if (err)
		return err;

	if (!fscrypt_has_permitted_context(dir, inode))
		return -EPERM;

	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_link);

int __fscrypt_prepare_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	int err;

	err = fscrypt_require_key(old_dir);
	if (err)
		return err;

	err = fscrypt_require_key(new_dir);
	if (err)
		return err;

	if (old_dir != new_dir) {
		if (IS_ENCRYPTED(new_dir) &&
		    !fscrypt_has_permitted_context(new_dir,
						   d_inode(old_dentry)))
			return -EPERM;

		if ((flags & RENAME_EXCHANGE) &&
		    IS_ENCRYPTED(old_dir) &&
		    !fscrypt_has_permitted_context(old_dir,
						   d_inode(new_dentry)))
			return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_rename);

int __fscrypt_prepare_lookup(struct inode *dir, struct dentry *dentry)
{
	int err = fscrypt_get_encryption_info(dir);

	if (err)
		return err;

	if (fscrypt_has_encryption_key(dir)) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_ENCRYPTED_WITH_KEY;
		spin_unlock(&dentry->d_lock);
	}

	d_set_d_op(dentry, &fscrypt_d_ops);
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_lookup);

int __fscrypt_prepare_symlink(struct inode *dir, unsigned int len,
			      unsigned int max_len,
			      struct fscrypt_str *disk_link)
{
	int err;

	/*
	 * To calculate the size of the encrypted symlink target we need to know
	 * the amount of NUL padding, which is determined by the flags set in
	 * the encryption policy which will be inherited from the directory.
	 * The easiest way to get access to this is to just load the directory's
	 * fscrypt_info, since we'll need it to create the dir_entry anyway.
	 *
	 * Note: in test_dummy_encryption mode, @dir may be unencrypted.
	 */
	err = fscrypt_get_encryption_info(dir);
	if (err)
		return err;
	if (!fscrypt_has_encryption_key(dir))
		return -ENOKEY;

	/*
	 * Calculate the size of the encrypted symlink and verify it won't
	 * exceed max_len.  Note that for historical reasons, encrypted symlink
	 * targets are prefixed with the ciphertext length, despite this
	 * actually being redundant with i_size.  This decreases by 2 bytes the
	 * longest symlink target we can accept.
	 *
	 * We could recover 1 byte by not counting a null terminator, but
	 * counting it (even though it is meaningless for ciphertext) is simpler
	 * for now since filesystems will assume it is there and subtract it.
	 */
	if (!fscrypt_fname_encrypted_size(dir, len,
					  max_len - sizeof(struct fscrypt_symlink_data),
					  &disk_link->len))
		return -ENAMETOOLONG;
	disk_link->len += sizeof(struct fscrypt_symlink_data);

	disk_link->name = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_symlink);

int __fscrypt_encrypt_symlink(struct inode *inode, const char *target,
			      unsigned int len, struct fscrypt_str *disk_link)
{
	int err;
	struct qstr iname = { .name = target, .len = len };
	struct fscrypt_symlink_data *sd;
	unsigned int ciphertext_len;

	err = fscrypt_require_key(inode);
	if (err)
		return err;

	if (disk_link->name) {
		/* filesystem-provided buffer */
		sd = (struct fscrypt_symlink_data *)disk_link->name;
	} else {
		sd = kmalloc(disk_link->len, GFP_NOFS);
		if (!sd)
			return -ENOMEM;
	}
	ciphertext_len = disk_link->len - sizeof(*sd);
	sd->len = cpu_to_le16(ciphertext_len);

	err = fname_encrypt(inode, &iname, sd->encrypted_path, ciphertext_len);
	if (err) {
		if (!disk_link->name)
			kfree(sd);
		return err;
	}
	/*
	 * Null-terminating the ciphertext doesn't make sense, but we still
	 * count the null terminator in the length, so we might as well
	 * initialize it just in case the filesystem writes it out.
	 */
	sd->encrypted_path[ciphertext_len] = '\0';

	if (!disk_link->name)
		disk_link->name = (unsigned char *)sd;
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_encrypt_symlink);

/**
 * fscrypt_get_symlink - get the target of an encrypted symlink
 * @inode: the symlink inode
 * @caddr: the on-disk contents of the symlink
 * @max_size: size of @caddr buffer
 * @done: if successful, will be set up to free the returned target
 *
 * If the symlink's encryption key is available, we decrypt its target.
 * Otherwise, we encode its target for presentation.
 *
 * This may sleep, so the filesystem must have dropped out of RCU mode already.
 *
 * Return: the presentable symlink target or an ERR_PTR()
 */
void *fscrypt_get_symlink(struct inode *inode, const void *caddr,
				unsigned int max_size)
{
	const struct fscrypt_symlink_data *sd;
	struct fscrypt_str cstr, pstr;
	int err;

	/* This is for encrypted symlinks only */
	if (WARN_ON(!IS_ENCRYPTED(inode)))
		return ERR_PTR(-EINVAL);

	/*
	 * Try to set up the symlink's encryption key, but we can continue
	 * regardless of whether the key is available or not.
	 */
	err = fscrypt_get_encryption_info(inode);
	if (err)
		return ERR_PTR(err);

	/*
	 * For historical reasons, encrypted symlink targets are prefixed with
	 * the ciphertext length, even though this is redundant with i_size.
	 */

	if (max_size < sizeof(*sd))
		return ERR_PTR(-EUCLEAN);
	sd = caddr;
	cstr.name = (unsigned char *)sd->encrypted_path;
	cstr.len = le16_to_cpu(sd->len);

	if (cstr.len == 0)
		return ERR_PTR(-EUCLEAN);

	if (cstr.len + sizeof(*sd) - 1 > max_size)
		return ERR_PTR(-EUCLEAN);

	err = fscrypt_fname_alloc_buffer(inode, cstr.len, &pstr);
	if (err)
		return ERR_PTR(err);

	err = fscrypt_fname_disk_to_usr(inode, 0, 0, &cstr, &pstr);
	if (err)
		goto err_kfree;

	err = -EUCLEAN;
	if (pstr.name[0] == '\0')
		goto err_kfree;

	pstr.name[pstr.len] = '\0';
	return pstr.name;

err_kfree:
	kfree(pstr.name);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(fscrypt_get_symlink);
