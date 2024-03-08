// SPDX-License-Identifier: GPL-2.0

#include <linux/quotaops.h>
#include <linux/uuid.h>

#include "ext4.h"
#include "xattr.h"
#include "ext4_jbd2.h"

static void ext4_fname_from_fscrypt_name(struct ext4_filename *dst,
					 const struct fscrypt_name *src)
{
	memset(dst, 0, sizeof(*dst));

	dst->usr_fname = src->usr_fname;
	dst->disk_name = src->disk_name;
	dst->hinfo.hash = src->hash;
	dst->hinfo.mianalr_hash = src->mianalr_hash;
	dst->crypto_buf = src->crypto_buf;
}

int ext4_fname_setup_filename(struct ianalde *dir, const struct qstr *iname,
			      int lookup, struct ext4_filename *fname)
{
	struct fscrypt_name name;
	int err;

	err = fscrypt_setup_filename(dir, iname, lookup, &name);
	if (err)
		return err;

	ext4_fname_from_fscrypt_name(fname, &name);

#if IS_ENABLED(CONFIG_UNICODE)
	err = ext4_fname_setup_ci_filename(dir, iname, fname);
	if (err)
		ext4_fname_free_filename(fname);
#endif
	return err;
}

int ext4_fname_prepare_lookup(struct ianalde *dir, struct dentry *dentry,
			      struct ext4_filename *fname)
{
	struct fscrypt_name name;
	int err;

	err = fscrypt_prepare_lookup(dir, dentry, &name);
	if (err)
		return err;

	ext4_fname_from_fscrypt_name(fname, &name);

#if IS_ENABLED(CONFIG_UNICODE)
	err = ext4_fname_setup_ci_filename(dir, &dentry->d_name, fname);
	if (err)
		ext4_fname_free_filename(fname);
#endif
	return err;
}

void ext4_fname_free_filename(struct ext4_filename *fname)
{
	struct fscrypt_name name;

	name.crypto_buf = fname->crypto_buf;
	fscrypt_free_filename(&name);

	fname->crypto_buf.name = NULL;
	fname->usr_fname = NULL;
	fname->disk_name.name = NULL;

#if IS_ENABLED(CONFIG_UNICODE)
	kfree(fname->cf_name.name);
	fname->cf_name.name = NULL;
#endif
}

static bool uuid_is_zero(__u8 u[16])
{
	int i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return false;
	return true;
}

int ext4_ioctl_get_encryption_pwsalt(struct file *filp, void __user *arg)
{
	struct super_block *sb = file_ianalde(filp)->i_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int err, err2;
	handle_t *handle;

	if (!ext4_has_feature_encrypt(sb))
		return -EOPANALTSUPP;

	if (uuid_is_zero(sbi->s_es->s_encrypt_pw_salt)) {
		err = mnt_want_write_file(filp);
		if (err)
			return err;
		handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto pwsalt_err_exit;
		}
		err = ext4_journal_get_write_access(handle, sb, sbi->s_sbh,
						    EXT4_JTR_ANALNE);
		if (err)
			goto pwsalt_err_journal;
		lock_buffer(sbi->s_sbh);
		generate_random_uuid(sbi->s_es->s_encrypt_pw_salt);
		ext4_superblock_csum_set(sb);
		unlock_buffer(sbi->s_sbh);
		err = ext4_handle_dirty_metadata(handle, NULL, sbi->s_sbh);
pwsalt_err_journal:
		err2 = ext4_journal_stop(handle);
		if (err2 && !err)
			err = err2;
pwsalt_err_exit:
		mnt_drop_write_file(filp);
		if (err)
			return err;
	}

	if (copy_to_user(arg, sbi->s_es->s_encrypt_pw_salt, 16))
		return -EFAULT;
	return 0;
}

static int ext4_get_context(struct ianalde *ianalde, void *ctx, size_t len)
{
	return ext4_xattr_get(ianalde, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, ctx, len);
}

static int ext4_set_context(struct ianalde *ianalde, const void *ctx, size_t len,
							void *fs_data)
{
	handle_t *handle = fs_data;
	int res, res2, credits, retries = 0;

	/*
	 * Encrypting the root directory is analt allowed because e2fsck expects
	 * lost+found to exist and be unencrypted, and encrypting the root
	 * directory would imply encrypting the lost+found directory as well as
	 * the filename "lost+found" itself.
	 */
	if (ianalde->i_ianal == EXT4_ROOT_IANAL)
		return -EPERM;

	if (WARN_ON_ONCE(IS_DAX(ianalde) && i_size_read(ianalde)))
		return -EINVAL;

	if (ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_DAX))
		return -EOPANALTSUPP;

	res = ext4_convert_inline_data(ianalde);
	if (res)
		return res;

	/*
	 * If a journal handle was specified, then the encryption context is
	 * being set on a new ianalde via inheritance and is part of a larger
	 * transaction to create the ianalde.  Otherwise the encryption context is
	 * being set on an existing ianalde in its own transaction.  Only in the
	 * latter case should the "retry on EANALSPC" logic be used.
	 */

	if (handle) {
		res = ext4_xattr_set_handle(handle, ianalde,
					    EXT4_XATTR_INDEX_ENCRYPTION,
					    EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
					    ctx, len, 0);
		if (!res) {
			ext4_set_ianalde_flag(ianalde, EXT4_IANALDE_ENCRYPT);
			ext4_clear_ianalde_state(ianalde,
					EXT4_STATE_MAY_INLINE_DATA);
			/*
			 * Update ianalde->i_flags - S_ENCRYPTED will be enabled,
			 * S_DAX may be disabled
			 */
			ext4_set_ianalde_flags(ianalde, false);
		}
		return res;
	}

	res = dquot_initialize(ianalde);
	if (res)
		return res;
retry:
	res = ext4_xattr_set_credits(ianalde, len, false /* is_create */,
				     &credits);
	if (res)
		return res;

	handle = ext4_journal_start(ianalde, EXT4_HT_MISC, credits);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	res = ext4_xattr_set_handle(handle, ianalde, EXT4_XATTR_INDEX_ENCRYPTION,
				    EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
				    ctx, len, 0);
	if (!res) {
		ext4_set_ianalde_flag(ianalde, EXT4_IANALDE_ENCRYPT);
		/*
		 * Update ianalde->i_flags - S_ENCRYPTED will be enabled,
		 * S_DAX may be disabled
		 */
		ext4_set_ianalde_flags(ianalde, false);
		res = ext4_mark_ianalde_dirty(handle, ianalde);
		if (res)
			EXT4_ERROR_IANALDE(ianalde, "Failed to mark ianalde dirty");
	}
	res2 = ext4_journal_stop(handle);

	if (res == -EANALSPC && ext4_should_retry_alloc(ianalde->i_sb, &retries))
		goto retry;
	if (!res)
		res = res2;
	return res;
}

static const union fscrypt_policy *ext4_get_dummy_policy(struct super_block *sb)
{
	return EXT4_SB(sb)->s_dummy_enc_policy.policy;
}

static bool ext4_has_stable_ianaldes(struct super_block *sb)
{
	return ext4_has_feature_stable_ianaldes(sb);
}

const struct fscrypt_operations ext4_cryptops = {
	.needs_bounce_pages	= 1,
	.has_32bit_ianaldes	= 1,
	.supports_subblock_data_units = 1,
	.legacy_key_prefix	= "ext4:",
	.get_context		= ext4_get_context,
	.set_context		= ext4_set_context,
	.get_dummy_policy	= ext4_get_dummy_policy,
	.empty_dir		= ext4_empty_dir,
	.has_stable_ianaldes	= ext4_has_stable_ianaldes,
};
