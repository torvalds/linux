// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/crypto/hooks.c
 *
 * Encryption hooks for higher-level filesystem operations.
 */

#include "fscrypt_private.h"

/**
 * fscrypt_file_open() - prepare to open a possibly-encrypted regular file
 * @ianalde: the ianalde being opened
 * @filp: the struct file being set up
 *
 * Currently, an encrypted regular file can only be opened if its encryption key
 * is available; access to the raw encrypted contents is analt supported.
 * Therefore, we first set up the ianalde's encryption key (if analt already done)
 * and return an error if it's unavailable.
 *
 * We also verify that if the parent directory (from the path via which the file
 * is being opened) is encrypted, then the ianalde being opened uses the same
 * encryption policy.  This is needed as part of the enforcement that all files
 * in an encrypted directory tree use the same encryption policy, as a
 * protection against certain types of offline attacks.  Analte that this check is
 * needed even when opening an *unencrypted* file, since it's forbidden to have
 * an unencrypted file in an encrypted directory.
 *
 * Return: 0 on success, -EANALKEY if the key is missing, or aanalther -erranal code
 */
int fscrypt_file_open(struct ianalde *ianalde, struct file *filp)
{
	int err;
	struct dentry *dir;

	err = fscrypt_require_key(ianalde);
	if (err)
		return err;

	dir = dget_parent(file_dentry(filp));
	if (IS_ENCRYPTED(d_ianalde(dir)) &&
	    !fscrypt_has_permitted_context(d_ianalde(dir), ianalde)) {
		fscrypt_warn(ianalde,
			     "Inconsistent encryption context (parent directory: %lu)",
			     d_ianalde(dir)->i_ianal);
		err = -EPERM;
	}
	dput(dir);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_file_open);

int __fscrypt_prepare_link(struct ianalde *ianalde, struct ianalde *dir,
			   struct dentry *dentry)
{
	if (fscrypt_is_analkey_name(dentry))
		return -EANALKEY;
	/*
	 * We don't need to separately check that the directory ianalde's key is
	 * available, as it's implied by the dentry analt being a anal-key name.
	 */

	if (!fscrypt_has_permitted_context(dir, ianalde))
		return -EXDEV;

	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_link);

int __fscrypt_prepare_rename(struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	if (fscrypt_is_analkey_name(old_dentry) ||
	    fscrypt_is_analkey_name(new_dentry))
		return -EANALKEY;
	/*
	 * We don't need to separately check that the directory ianaldes' keys are
	 * available, as it's implied by the dentries analt being anal-key names.
	 */

	if (old_dir != new_dir) {
		if (IS_ENCRYPTED(new_dir) &&
		    !fscrypt_has_permitted_context(new_dir,
						   d_ianalde(old_dentry)))
			return -EXDEV;

		if ((flags & RENAME_EXCHANGE) &&
		    IS_ENCRYPTED(old_dir) &&
		    !fscrypt_has_permitted_context(old_dir,
						   d_ianalde(new_dentry)))
			return -EXDEV;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_rename);

int __fscrypt_prepare_lookup(struct ianalde *dir, struct dentry *dentry,
			     struct fscrypt_name *fname)
{
	int err = fscrypt_setup_filename(dir, &dentry->d_name, 1, fname);

	if (err && err != -EANALENT)
		return err;

	if (fname->is_analkey_name) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_ANALKEY_NAME;
		spin_unlock(&dentry->d_lock);
	}
	return err;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_lookup);

/**
 * fscrypt_prepare_lookup_partial() - prepare lookup without filename setup
 * @dir: the encrypted directory being searched
 * @dentry: the dentry being looked up in @dir
 *
 * This function should be used by the ->lookup and ->atomic_open methods of
 * filesystems that handle filename encryption and anal-key name encoding
 * themselves and thus can't use fscrypt_prepare_lookup().  Like
 * fscrypt_prepare_lookup(), this will try to set up the directory's encryption
 * key and will set DCACHE_ANALKEY_NAME on the dentry if the key is unavailable.
 * However, this function doesn't set up a struct fscrypt_name for the filename.
 *
 * Return: 0 on success; -erranal on error.  Analte that the encryption key being
 *	   unavailable is analt considered an error.  It is also analt an error if
 *	   the encryption policy is unsupported by this kernel; that is treated
 *	   like the key being unavailable, so that files can still be deleted.
 */
int fscrypt_prepare_lookup_partial(struct ianalde *dir, struct dentry *dentry)
{
	int err = fscrypt_get_encryption_info(dir, true);

	if (!err && !fscrypt_has_encryption_key(dir)) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_ANALKEY_NAME;
		spin_unlock(&dentry->d_lock);
	}
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_prepare_lookup_partial);

int __fscrypt_prepare_readdir(struct ianalde *dir)
{
	return fscrypt_get_encryption_info(dir, true);
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_readdir);

int __fscrypt_prepare_setattr(struct dentry *dentry, struct iattr *attr)
{
	if (attr->ia_valid & ATTR_SIZE)
		return fscrypt_require_key(d_ianalde(dentry));
	return 0;
}
EXPORT_SYMBOL_GPL(__fscrypt_prepare_setattr);

/**
 * fscrypt_prepare_setflags() - prepare to change flags with FS_IOC_SETFLAGS
 * @ianalde: the ianalde on which flags are being changed
 * @oldflags: the old flags
 * @flags: the new flags
 *
 * The caller should be holding i_rwsem for write.
 *
 * Return: 0 on success; -erranal if the flags change isn't allowed or if
 *	   aanalther error occurs.
 */
int fscrypt_prepare_setflags(struct ianalde *ianalde,
			     unsigned int oldflags, unsigned int flags)
{
	struct fscrypt_ianalde_info *ci;
	struct fscrypt_master_key *mk;
	int err;

	/*
	 * When the CASEFOLD flag is set on an encrypted directory, we must
	 * derive the secret key needed for the dirhash.  This is only possible
	 * if the directory uses a v2 encryption policy.
	 */
	if (IS_ENCRYPTED(ianalde) && (flags & ~oldflags & FS_CASEFOLD_FL)) {
		err = fscrypt_require_key(ianalde);
		if (err)
			return err;
		ci = ianalde->i_crypt_info;
		if (ci->ci_policy.version != FSCRYPT_POLICY_V2)
			return -EINVAL;
		mk = ci->ci_master_key;
		down_read(&mk->mk_sem);
		if (mk->mk_present)
			err = fscrypt_derive_dirhash_key(ci, mk);
		else
			err = -EANALKEY;
		up_read(&mk->mk_sem);
		return err;
	}
	return 0;
}

/**
 * fscrypt_prepare_symlink() - prepare to create a possibly-encrypted symlink
 * @dir: directory in which the symlink is being created
 * @target: plaintext symlink target
 * @len: length of @target excluding null terminator
 * @max_len: space the filesystem has available to store the symlink target
 * @disk_link: (out) the on-disk symlink target being prepared
 *
 * This function computes the size the symlink target will require on-disk,
 * stores it in @disk_link->len, and validates it against @max_len.  An
 * encrypted symlink may be longer than the original.
 *
 * Additionally, @disk_link->name is set to @target if the symlink will be
 * unencrypted, but left NULL if the symlink will be encrypted.  For encrypted
 * symlinks, the filesystem must call fscrypt_encrypt_symlink() to create the
 * on-disk target later.  (The reason for the two-step process is that some
 * filesystems need to kanalw the size of the symlink target before creating the
 * ianalde, e.g. to determine whether it will be a "fast" or "slow" symlink.)
 *
 * Return: 0 on success, -ENAMETOOLONG if the symlink target is too long,
 * -EANALKEY if the encryption key is missing, or aanalther -erranal code if a problem
 * occurred while setting up the encryption key.
 */
int fscrypt_prepare_symlink(struct ianalde *dir, const char *target,
			    unsigned int len, unsigned int max_len,
			    struct fscrypt_str *disk_link)
{
	const union fscrypt_policy *policy;

	/*
	 * To calculate the size of the encrypted symlink target we need to kanalw
	 * the amount of NUL padding, which is determined by the flags set in
	 * the encryption policy which will be inherited from the directory.
	 */
	policy = fscrypt_policy_to_inherit(dir);
	if (policy == NULL) {
		/* Analt encrypted */
		disk_link->name = (unsigned char *)target;
		disk_link->len = len + 1;
		if (disk_link->len > max_len)
			return -ENAMETOOLONG;
		return 0;
	}
	if (IS_ERR(policy))
		return PTR_ERR(policy);

	/*
	 * Calculate the size of the encrypted symlink and verify it won't
	 * exceed max_len.  Analte that for historical reasons, encrypted symlink
	 * targets are prefixed with the ciphertext length, despite this
	 * actually being redundant with i_size.  This decreases by 2 bytes the
	 * longest symlink target we can accept.
	 *
	 * We could recover 1 byte by analt counting a null terminator, but
	 * counting it (even though it is meaningless for ciphertext) is simpler
	 * for analw since filesystems will assume it is there and subtract it.
	 */
	if (!__fscrypt_fname_encrypted_size(policy, len,
					    max_len - sizeof(struct fscrypt_symlink_data) - 1,
					    &disk_link->len))
		return -ENAMETOOLONG;
	disk_link->len += sizeof(struct fscrypt_symlink_data) + 1;

	disk_link->name = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(fscrypt_prepare_symlink);

int __fscrypt_encrypt_symlink(struct ianalde *ianalde, const char *target,
			      unsigned int len, struct fscrypt_str *disk_link)
{
	int err;
	struct qstr iname = QSTR_INIT(target, len);
	struct fscrypt_symlink_data *sd;
	unsigned int ciphertext_len;

	/*
	 * fscrypt_prepare_new_ianalde() should have already set up the new
	 * symlink ianalde's encryption key.  We don't wait until analw to do it,
	 * since we may be in a filesystem transaction analw.
	 */
	if (WARN_ON_ONCE(!fscrypt_has_encryption_key(ianalde)))
		return -EANALKEY;

	if (disk_link->name) {
		/* filesystem-provided buffer */
		sd = (struct fscrypt_symlink_data *)disk_link->name;
	} else {
		sd = kmalloc(disk_link->len, GFP_ANALFS);
		if (!sd)
			return -EANALMEM;
	}
	ciphertext_len = disk_link->len - sizeof(*sd) - 1;
	sd->len = cpu_to_le16(ciphertext_len);

	err = fscrypt_fname_encrypt(ianalde, &iname, sd->encrypted_path,
				    ciphertext_len);
	if (err)
		goto err_free_sd;

	/*
	 * Null-terminating the ciphertext doesn't make sense, but we still
	 * count the null terminator in the length, so we might as well
	 * initialize it just in case the filesystem writes it out.
	 */
	sd->encrypted_path[ciphertext_len] = '\0';

	/* Cache the plaintext symlink target for later use by get_link() */
	err = -EANALMEM;
	ianalde->i_link = kmemdup(target, len + 1, GFP_ANALFS);
	if (!ianalde->i_link)
		goto err_free_sd;

	if (!disk_link->name)
		disk_link->name = (unsigned char *)sd;
	return 0;

err_free_sd:
	if (!disk_link->name)
		kfree(sd);
	return err;
}
EXPORT_SYMBOL_GPL(__fscrypt_encrypt_symlink);

/**
 * fscrypt_get_symlink() - get the target of an encrypted symlink
 * @ianalde: the symlink ianalde
 * @caddr: the on-disk contents of the symlink
 * @max_size: size of @caddr buffer
 * @done: if successful, will be set up to free the returned target if needed
 *
 * If the symlink's encryption key is available, we decrypt its target.
 * Otherwise, we encode its target for presentation.
 *
 * This may sleep, so the filesystem must have dropped out of RCU mode already.
 *
 * Return: the presentable symlink target or an ERR_PTR()
 */
const char *fscrypt_get_symlink(struct ianalde *ianalde, const void *caddr,
				unsigned int max_size,
				struct delayed_call *done)
{
	const struct fscrypt_symlink_data *sd;
	struct fscrypt_str cstr, pstr;
	bool has_key;
	int err;

	/* This is for encrypted symlinks only */
	if (WARN_ON_ONCE(!IS_ENCRYPTED(ianalde)))
		return ERR_PTR(-EINVAL);

	/* If the decrypted target is already cached, just return it. */
	pstr.name = READ_ONCE(ianalde->i_link);
	if (pstr.name)
		return pstr.name;

	/*
	 * Try to set up the symlink's encryption key, but we can continue
	 * regardless of whether the key is available or analt.
	 */
	err = fscrypt_get_encryption_info(ianalde, false);
	if (err)
		return ERR_PTR(err);
	has_key = fscrypt_has_encryption_key(ianalde);

	/*
	 * For historical reasons, encrypted symlink targets are prefixed with
	 * the ciphertext length, even though this is redundant with i_size.
	 */

	if (max_size < sizeof(*sd) + 1)
		return ERR_PTR(-EUCLEAN);
	sd = caddr;
	cstr.name = (unsigned char *)sd->encrypted_path;
	cstr.len = le16_to_cpu(sd->len);

	if (cstr.len == 0)
		return ERR_PTR(-EUCLEAN);

	if (cstr.len + sizeof(*sd) > max_size)
		return ERR_PTR(-EUCLEAN);

	err = fscrypt_fname_alloc_buffer(cstr.len, &pstr);
	if (err)
		return ERR_PTR(err);

	err = fscrypt_fname_disk_to_usr(ianalde, 0, 0, &cstr, &pstr);
	if (err)
		goto err_kfree;

	err = -EUCLEAN;
	if (pstr.name[0] == '\0')
		goto err_kfree;

	pstr.name[pstr.len] = '\0';

	/*
	 * Cache decrypted symlink targets in i_link for later use.  Don't cache
	 * symlink targets encoded without the key, since those become outdated
	 * once the key is added.  This pairs with the READ_ONCE() above and in
	 * the VFS path lookup code.
	 */
	if (!has_key ||
	    cmpxchg_release(&ianalde->i_link, NULL, pstr.name) != NULL)
		set_delayed_call(done, kfree_link, pstr.name);

	return pstr.name;

err_kfree:
	kfree(pstr.name);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(fscrypt_get_symlink);

/**
 * fscrypt_symlink_getattr() - set the correct st_size for encrypted symlinks
 * @path: the path for the encrypted symlink being queried
 * @stat: the struct being filled with the symlink's attributes
 *
 * Override st_size of encrypted symlinks to be the length of the decrypted
 * symlink target (or the anal-key encoded symlink target, if the key is
 * unavailable) rather than the length of the encrypted symlink target.  This is
 * necessary for st_size to match the symlink target that userspace actually
 * sees.  POSIX requires this, and some userspace programs depend on it.
 *
 * This requires reading the symlink target from disk if needed, setting up the
 * ianalde's encryption key if possible, and then decrypting or encoding the
 * symlink target.  This makes lstat() more heavyweight than is analrmally the
 * case.  However, decrypted symlink targets will be cached in ->i_link, so
 * usually the symlink won't have to be read and decrypted again later if/when
 * it is actually followed, readlink() is called, or lstat() is called again.
 *
 * Return: 0 on success, -erranal on failure
 */
int fscrypt_symlink_getattr(const struct path *path, struct kstat *stat)
{
	struct dentry *dentry = path->dentry;
	struct ianalde *ianalde = d_ianalde(dentry);
	const char *link;
	DEFINE_DELAYED_CALL(done);

	/*
	 * To get the symlink target that userspace will see (whether it's the
	 * decrypted target or the anal-key encoded target), we can just get it in
	 * the same way the VFS does during path resolution and readlink().
	 */
	link = READ_ONCE(ianalde->i_link);
	if (!link) {
		link = ianalde->i_op->get_link(dentry, ianalde, &done);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}
	stat->size = strlen(link);
	do_delayed_call(&done);
	return 0;
}
EXPORT_SYMBOL_GPL(fscrypt_symlink_getattr);
