// SPDX-License-Identifier: GPL-2.0
/*
 * Encryption policy functions for per-file encryption support.
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility.
 *
 * Originally written by Michael Halcrow, 2015.
 * Modified by Jaegeuk Kim, 2015.
 * Modified by Eric Biggers, 2019 for v2 policy support.
 */

#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/mount.h>
#include "fscrypt_private.h"

/**
 * fscrypt_policies_equal() - check whether two encryption policies are the same
 * @policy1: the first policy
 * @policy2: the second policy
 *
 * Return: %true if equal, else %false
 */
bool fscrypt_policies_equal(const union fscrypt_policy *policy1,
			    const union fscrypt_policy *policy2)
{
	if (policy1->version != policy2->version)
		return false;

	return !memcmp(policy1, policy2, fscrypt_policy_size(policy1));
}

static const union fscrypt_policy *
fscrypt_get_dummy_policy(struct super_block *sb)
{
	if (!sb->s_cop->get_dummy_policy)
		return NULL;
	return sb->s_cop->get_dummy_policy(sb);
}

static bool fscrypt_valid_enc_modes(u32 contents_mode, u32 filenames_mode)
{
	if (contents_mode == FSCRYPT_MODE_AES_256_XTS &&
	    filenames_mode == FSCRYPT_MODE_AES_256_CTS)
		return true;

	if (contents_mode == FSCRYPT_MODE_AES_128_CBC &&
	    filenames_mode == FSCRYPT_MODE_AES_128_CTS)
		return true;

	if (contents_mode == FSCRYPT_MODE_ADIANTUM &&
	    filenames_mode == FSCRYPT_MODE_ADIANTUM)
		return true;

	return false;
}

static bool supported_direct_key_modes(const struct inode *inode,
				       u32 contents_mode, u32 filenames_mode)
{
	const struct fscrypt_mode *mode;

	if (contents_mode != filenames_mode) {
		fscrypt_warn(inode,
			     "Direct key flag not allowed with different contents and filenames modes");
		return false;
	}
	mode = &fscrypt_modes[contents_mode];

	if (mode->ivsize < offsetofend(union fscrypt_iv, nonce)) {
		fscrypt_warn(inode, "Direct key flag not allowed with %s",
			     mode->friendly_name);
		return false;
	}
	return true;
}

static bool supported_iv_ino_lblk_policy(const struct fscrypt_policy_v2 *policy,
					 const struct inode *inode,
					 const char *type,
					 int max_ino_bits, int max_lblk_bits)
{
	struct super_block *sb = inode->i_sb;
	int ino_bits = 64, lblk_bits = 64;

	/*
	 * IV_INO_LBLK_* exist only because of hardware limitations, and
	 * currently the only known use case for them involves AES-256-XTS.
	 * That's also all we test currently.  For these reasons, for now only
	 * allow AES-256-XTS here.  This can be relaxed later if a use case for
	 * IV_INO_LBLK_* with other encryption modes arises.
	 */
	if (policy->contents_encryption_mode != FSCRYPT_MODE_AES_256_XTS) {
		fscrypt_warn(inode,
			     "Can't use %s policy with contents mode other than AES-256-XTS",
			     type);
		return false;
	}

	/*
	 * It's unsafe to include inode numbers in the IVs if the filesystem can
	 * potentially renumber inodes, e.g. via filesystem shrinking.
	 */
	if (!sb->s_cop->has_stable_inodes ||
	    !sb->s_cop->has_stable_inodes(sb)) {
		fscrypt_warn(inode,
			     "Can't use %s policy on filesystem '%s' because it doesn't have stable inode numbers",
			     type, sb->s_id);
		return false;
	}
	if (sb->s_cop->get_ino_and_lblk_bits)
		sb->s_cop->get_ino_and_lblk_bits(sb, &ino_bits, &lblk_bits);
	if (ino_bits > max_ino_bits) {
		fscrypt_warn(inode,
			     "Can't use %s policy on filesystem '%s' because its inode numbers are too long",
			     type, sb->s_id);
		return false;
	}
	if (lblk_bits > max_lblk_bits) {
		fscrypt_warn(inode,
			     "Can't use %s policy on filesystem '%s' because its block numbers are too long",
			     type, sb->s_id);
		return false;
	}
	return true;
}

static bool fscrypt_supported_v1_policy(const struct fscrypt_policy_v1 *policy,
					const struct inode *inode)
{
	if (!fscrypt_valid_enc_modes(policy->contents_encryption_mode,
				     policy->filenames_encryption_mode)) {
		fscrypt_warn(inode,
			     "Unsupported encryption modes (contents %d, filenames %d)",
			     policy->contents_encryption_mode,
			     policy->filenames_encryption_mode);
		return false;
	}

	if (policy->flags & ~(FSCRYPT_POLICY_FLAGS_PAD_MASK |
			      FSCRYPT_POLICY_FLAG_DIRECT_KEY)) {
		fscrypt_warn(inode, "Unsupported encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	if ((policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) &&
	    !supported_direct_key_modes(inode, policy->contents_encryption_mode,
					policy->filenames_encryption_mode))
		return false;

	if (IS_CASEFOLDED(inode)) {
		/* With v1, there's no way to derive dirhash keys. */
		fscrypt_warn(inode,
			     "v1 policies can't be used on casefolded directories");
		return false;
	}

	return true;
}

static bool fscrypt_supported_v2_policy(const struct fscrypt_policy_v2 *policy,
					const struct inode *inode)
{
	int count = 0;

	if (!fscrypt_valid_enc_modes(policy->contents_encryption_mode,
				     policy->filenames_encryption_mode)) {
		fscrypt_warn(inode,
			     "Unsupported encryption modes (contents %d, filenames %d)",
			     policy->contents_encryption_mode,
			     policy->filenames_encryption_mode);
		return false;
	}

	if (policy->flags & ~(FSCRYPT_POLICY_FLAGS_PAD_MASK |
			      FSCRYPT_POLICY_FLAG_DIRECT_KEY |
			      FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64 |
			      FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32)) {
		fscrypt_warn(inode, "Unsupported encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY);
	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64);
	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32);
	if (count > 1) {
		fscrypt_warn(inode, "Mutually exclusive encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	if ((policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) &&
	    !supported_direct_key_modes(inode, policy->contents_encryption_mode,
					policy->filenames_encryption_mode))
		return false;

	if ((policy->flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64) &&
	    !supported_iv_ino_lblk_policy(policy, inode, "IV_INO_LBLK_64",
					  32, 32))
		return false;

	/*
	 * IV_INO_LBLK_32 hashes the inode number, so in principle it can
	 * support any ino_bits.  However, currently the inode number is gotten
	 * from inode::i_ino which is 'unsigned long'.  So for now the
	 * implementation limit is 32 bits.
	 */
	if ((policy->flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32) &&
	    !supported_iv_ino_lblk_policy(policy, inode, "IV_INO_LBLK_32",
					  32, 32))
		return false;

	if (memchr_inv(policy->__reserved, 0, sizeof(policy->__reserved))) {
		fscrypt_warn(inode, "Reserved bits set in encryption policy");
		return false;
	}

	return true;
}

/**
 * fscrypt_supported_policy() - check whether an encryption policy is supported
 * @policy_u: the encryption policy
 * @inode: the inode on which the policy will be used
 *
 * Given an encryption policy, check whether all its encryption modes and other
 * settings are supported by this kernel on the given inode.  (But we don't
 * currently don't check for crypto API support here, so attempting to use an
 * algorithm not configured into the crypto API will still fail later.)
 *
 * Return: %true if supported, else %false
 */
bool fscrypt_supported_policy(const union fscrypt_policy *policy_u,
			      const struct inode *inode)
{
	switch (policy_u->version) {
	case FSCRYPT_POLICY_V1:
		return fscrypt_supported_v1_policy(&policy_u->v1, inode);
	case FSCRYPT_POLICY_V2:
		return fscrypt_supported_v2_policy(&policy_u->v2, inode);
	}
	return false;
}

/**
 * fscrypt_new_context() - create a new fscrypt_context
 * @ctx_u: output context
 * @policy_u: input policy
 * @nonce: nonce to use
 *
 * Create an fscrypt_context for an inode that is being assigned the given
 * encryption policy.  @nonce must be a new random nonce.
 *
 * Return: the size of the new context in bytes.
 */
static int fscrypt_new_context(union fscrypt_context *ctx_u,
			       const union fscrypt_policy *policy_u,
			       const u8 nonce[FSCRYPT_FILE_NONCE_SIZE])
{
	memset(ctx_u, 0, sizeof(*ctx_u));

	switch (policy_u->version) {
	case FSCRYPT_POLICY_V1: {
		const struct fscrypt_policy_v1 *policy = &policy_u->v1;
		struct fscrypt_context_v1 *ctx = &ctx_u->v1;

		ctx->version = FSCRYPT_CONTEXT_V1;
		ctx->contents_encryption_mode =
			policy->contents_encryption_mode;
		ctx->filenames_encryption_mode =
			policy->filenames_encryption_mode;
		ctx->flags = policy->flags;
		memcpy(ctx->master_key_descriptor,
		       policy->master_key_descriptor,
		       sizeof(ctx->master_key_descriptor));
		memcpy(ctx->nonce, nonce, FSCRYPT_FILE_NONCE_SIZE);
		return sizeof(*ctx);
	}
	case FSCRYPT_POLICY_V2: {
		const struct fscrypt_policy_v2 *policy = &policy_u->v2;
		struct fscrypt_context_v2 *ctx = &ctx_u->v2;

		ctx->version = FSCRYPT_CONTEXT_V2;
		ctx->contents_encryption_mode =
			policy->contents_encryption_mode;
		ctx->filenames_encryption_mode =
			policy->filenames_encryption_mode;
		ctx->flags = policy->flags;
		memcpy(ctx->master_key_identifier,
		       policy->master_key_identifier,
		       sizeof(ctx->master_key_identifier));
		memcpy(ctx->nonce, nonce, FSCRYPT_FILE_NONCE_SIZE);
		return sizeof(*ctx);
	}
	}
	BUG();
}

/**
 * fscrypt_policy_from_context() - convert an fscrypt_context to
 *				   an fscrypt_policy
 * @policy_u: output policy
 * @ctx_u: input context
 * @ctx_size: size of input context in bytes
 *
 * Given an fscrypt_context, build the corresponding fscrypt_policy.
 *
 * Return: 0 on success, or -EINVAL if the fscrypt_context has an unrecognized
 * version number or size.
 *
 * This does *not* validate the settings within the policy itself, e.g. the
 * modes, flags, and reserved bits.  Use fscrypt_supported_policy() for that.
 */
int fscrypt_policy_from_context(union fscrypt_policy *policy_u,
				const union fscrypt_context *ctx_u,
				int ctx_size)
{
	memset(policy_u, 0, sizeof(*policy_u));

	if (!fscrypt_context_is_valid(ctx_u, ctx_size))
		return -EINVAL;

	switch (ctx_u->version) {
	case FSCRYPT_CONTEXT_V1: {
		const struct fscrypt_context_v1 *ctx = &ctx_u->v1;
		struct fscrypt_policy_v1 *policy = &policy_u->v1;

		policy->version = FSCRYPT_POLICY_V1;
		policy->contents_encryption_mode =
			ctx->contents_encryption_mode;
		policy->filenames_encryption_mode =
			ctx->filenames_encryption_mode;
		policy->flags = ctx->flags;
		memcpy(policy->master_key_descriptor,
		       ctx->master_key_descriptor,
		       sizeof(policy->master_key_descriptor));
		return 0;
	}
	case FSCRYPT_CONTEXT_V2: {
		const struct fscrypt_context_v2 *ctx = &ctx_u->v2;
		struct fscrypt_policy_v2 *policy = &policy_u->v2;

		policy->version = FSCRYPT_POLICY_V2;
		policy->contents_encryption_mode =
			ctx->contents_encryption_mode;
		policy->filenames_encryption_mode =
			ctx->filenames_encryption_mode;
		policy->flags = ctx->flags;
		memcpy(policy->__reserved, ctx->__reserved,
		       sizeof(policy->__reserved));
		memcpy(policy->master_key_identifier,
		       ctx->master_key_identifier,
		       sizeof(policy->master_key_identifier));
		return 0;
	}
	}
	/* unreachable */
	return -EINVAL;
}

/* Retrieve an inode's encryption policy */
static int fscrypt_get_policy(struct inode *inode, union fscrypt_policy *policy)
{
	const struct fscrypt_info *ci;
	union fscrypt_context ctx;
	int ret;

	ci = fscrypt_get_info(inode);
	if (ci) {
		/* key available, use the cached policy */
		*policy = ci->ci_policy;
		return 0;
	}

	if (!IS_ENCRYPTED(inode))
		return -ENODATA;

	ret = inode->i_sb->s_cop->get_context(inode, &ctx, sizeof(ctx));
	if (ret < 0)
		return (ret == -ERANGE) ? -EINVAL : ret;

	return fscrypt_policy_from_context(policy, &ctx, ret);
}

static int set_encryption_policy(struct inode *inode,
				 const union fscrypt_policy *policy)
{
	u8 nonce[FSCRYPT_FILE_NONCE_SIZE];
	union fscrypt_context ctx;
	int ctxsize;
	int err;

	if (!fscrypt_supported_policy(policy, inode))
		return -EINVAL;

	switch (policy->version) {
	case FSCRYPT_POLICY_V1:
		/*
		 * The original encryption policy version provided no way of
		 * verifying that the correct master key was supplied, which was
		 * insecure in scenarios where multiple users have access to the
		 * same encrypted files (even just read-only access).  The new
		 * encryption policy version fixes this and also implies use of
		 * an improved key derivation function and allows non-root users
		 * to securely remove keys.  So as long as compatibility with
		 * old kernels isn't required, it is recommended to use the new
		 * policy version for all new encrypted directories.
		 */
		pr_warn_once("%s (pid %d) is setting deprecated v1 encryption policy; recommend upgrading to v2.\n",
			     current->comm, current->pid);
		break;
	case FSCRYPT_POLICY_V2:
		err = fscrypt_verify_key_added(inode->i_sb,
					       policy->v2.master_key_identifier);
		if (err)
			return err;
		if (policy->v2.flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32)
			pr_warn_once("%s (pid %d) is setting an IV_INO_LBLK_32 encryption policy.  This should only be used if there are certain hardware limitations.\n",
				     current->comm, current->pid);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	get_random_bytes(nonce, FSCRYPT_FILE_NONCE_SIZE);
	ctxsize = fscrypt_new_context(&ctx, policy, nonce);

	return inode->i_sb->s_cop->set_context(inode, &ctx, ctxsize, NULL);
}

int fscrypt_ioctl_set_policy(struct file *filp, const void __user *arg)
{
	union fscrypt_policy policy;
	union fscrypt_policy existing_policy;
	struct inode *inode = file_inode(filp);
	u8 version;
	int size;
	int ret;

	if (get_user(policy.version, (const u8 __user *)arg))
		return -EFAULT;

	size = fscrypt_policy_size(&policy);
	if (size <= 0)
		return -EINVAL;

	/*
	 * We should just copy the remaining 'size - 1' bytes here, but a
	 * bizarre bug in gcc 7 and earlier (fixed by gcc r255731) causes gcc to
	 * think that size can be 0 here (despite the check above!) *and* that
	 * it's a compile-time constant.  Thus it would think copy_from_user()
	 * is passed compile-time constant ULONG_MAX, causing the compile-time
	 * buffer overflow check to fail, breaking the build. This only occurred
	 * when building an i386 kernel with -Os and branch profiling enabled.
	 *
	 * Work around it by just copying the first byte again...
	 */
	version = policy.version;
	if (copy_from_user(&policy, arg, size))
		return -EFAULT;
	policy.version = version;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	ret = fscrypt_get_policy(inode, &existing_policy);
	if (ret == -ENODATA) {
		if (!S_ISDIR(inode->i_mode))
			ret = -ENOTDIR;
		else if (IS_DEADDIR(inode))
			ret = -ENOENT;
		else if (!inode->i_sb->s_cop->empty_dir(inode))
			ret = -ENOTEMPTY;
		else
			ret = set_encryption_policy(inode, &policy);
	} else if (ret == -EINVAL ||
		   (ret == 0 && !fscrypt_policies_equal(&policy,
							&existing_policy))) {
		/* The file already uses a different encryption policy. */
		ret = -EEXIST;
	}

	inode_unlock(inode);

	mnt_drop_write_file(filp);
	return ret;
}
EXPORT_SYMBOL(fscrypt_ioctl_set_policy);

/* Original ioctl version; can only get the original policy version */
int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg)
{
	union fscrypt_policy policy;
	int err;

	err = fscrypt_get_policy(file_inode(filp), &policy);
	if (err)
		return err;

	if (policy.version != FSCRYPT_POLICY_V1)
		return -EINVAL;

	if (copy_to_user(arg, &policy, sizeof(policy.v1)))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL(fscrypt_ioctl_get_policy);

/* Extended ioctl version; can get policies of any version */
int fscrypt_ioctl_get_policy_ex(struct file *filp, void __user *uarg)
{
	struct fscrypt_get_policy_ex_arg arg;
	union fscrypt_policy *policy = (union fscrypt_policy *)&arg.policy;
	size_t policy_size;
	int err;

	/* arg is policy_size, then policy */
	BUILD_BUG_ON(offsetof(typeof(arg), policy_size) != 0);
	BUILD_BUG_ON(offsetofend(typeof(arg), policy_size) !=
		     offsetof(typeof(arg), policy));
	BUILD_BUG_ON(sizeof(arg.policy) != sizeof(*policy));

	err = fscrypt_get_policy(file_inode(filp), policy);
	if (err)
		return err;
	policy_size = fscrypt_policy_size(policy);

	if (copy_from_user(&arg, uarg, sizeof(arg.policy_size)))
		return -EFAULT;

	if (policy_size > arg.policy_size)
		return -EOVERFLOW;
	arg.policy_size = policy_size;

	if (copy_to_user(uarg, &arg, sizeof(arg.policy_size) + policy_size))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_get_policy_ex);

/* FS_IOC_GET_ENCRYPTION_NONCE: retrieve file's encryption nonce for testing */
int fscrypt_ioctl_get_nonce(struct file *filp, void __user *arg)
{
	struct inode *inode = file_inode(filp);
	union fscrypt_context ctx;
	int ret;

	ret = inode->i_sb->s_cop->get_context(inode, &ctx, sizeof(ctx));
	if (ret < 0)
		return ret;
	if (!fscrypt_context_is_valid(&ctx, ret))
		return -EINVAL;
	if (copy_to_user(arg, fscrypt_context_nonce(&ctx),
			 FSCRYPT_FILE_NONCE_SIZE))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_get_nonce);

/**
 * fscrypt_has_permitted_context() - is a file's encryption policy permitted
 *				     within its directory?
 *
 * @parent: inode for parent directory
 * @child: inode for file being looked up, opened, or linked into @parent
 *
 * Filesystems must call this before permitting access to an inode in a
 * situation where the parent directory is encrypted (either before allowing
 * ->lookup() to succeed, or for a regular file before allowing it to be opened)
 * and before any operation that involves linking an inode into an encrypted
 * directory, including link, rename, and cross rename.  It enforces the
 * constraint that within a given encrypted directory tree, all files use the
 * same encryption policy.  The pre-access check is needed to detect potentially
 * malicious offline violations of this constraint, while the link and rename
 * checks are needed to prevent online violations of this constraint.
 *
 * Return: 1 if permitted, 0 if forbidden.
 */
int fscrypt_has_permitted_context(struct inode *parent, struct inode *child)
{
	union fscrypt_policy parent_policy, child_policy;
	int err, err1, err2;

	/* No restrictions on file types which are never encrypted */
	if (!S_ISREG(child->i_mode) && !S_ISDIR(child->i_mode) &&
	    !S_ISLNK(child->i_mode))
		return 1;

	/* No restrictions if the parent directory is unencrypted */
	if (!IS_ENCRYPTED(parent))
		return 1;

	/* Encrypted directories must not contain unencrypted files */
	if (!IS_ENCRYPTED(child))
		return 0;

	/*
	 * Both parent and child are encrypted, so verify they use the same
	 * encryption policy.  Compare the fscrypt_info structs if the keys are
	 * available, otherwise retrieve and compare the fscrypt_contexts.
	 *
	 * Note that the fscrypt_context retrieval will be required frequently
	 * when accessing an encrypted directory tree without the key.
	 * Performance-wise this is not a big deal because we already don't
	 * really optimize for file access without the key (to the extent that
	 * such access is even possible), given that any attempted access
	 * already causes a fscrypt_context retrieval and keyring search.
	 *
	 * In any case, if an unexpected error occurs, fall back to "forbidden".
	 */

	err = fscrypt_get_encryption_info(parent, true);
	if (err)
		return 0;
	err = fscrypt_get_encryption_info(child, true);
	if (err)
		return 0;

	err1 = fscrypt_get_policy(parent, &parent_policy);
	err2 = fscrypt_get_policy(child, &child_policy);

	/*
	 * Allow the case where the parent and child both have an unrecognized
	 * encryption policy, so that files with an unrecognized encryption
	 * policy can be deleted.
	 */
	if (err1 == -EINVAL && err2 == -EINVAL)
		return 1;

	if (err1 || err2)
		return 0;

	return fscrypt_policies_equal(&parent_policy, &child_policy);
}
EXPORT_SYMBOL(fscrypt_has_permitted_context);

/*
 * Return the encryption policy that new files in the directory will inherit, or
 * NULL if none, or an ERR_PTR() on error.  If the directory is encrypted, also
 * ensure that its key is set up, so that the new filename can be encrypted.
 */
const union fscrypt_policy *fscrypt_policy_to_inherit(struct inode *dir)
{
	int err;

	if (IS_ENCRYPTED(dir)) {
		err = fscrypt_require_key(dir);
		if (err)
			return ERR_PTR(err);
		return &dir->i_crypt_info->ci_policy;
	}

	return fscrypt_get_dummy_policy(dir->i_sb);
}

/**
 * fscrypt_set_context() - Set the fscrypt context of a new inode
 * @inode: a new inode
 * @fs_data: private data given by FS and passed to ->set_context()
 *
 * This should be called after fscrypt_prepare_new_inode(), generally during a
 * filesystem transaction.  Everything here must be %GFP_NOFS-safe.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_set_context(struct inode *inode, void *fs_data)
{
	struct fscrypt_info *ci = inode->i_crypt_info;
	union fscrypt_context ctx;
	int ctxsize;

	/* fscrypt_prepare_new_inode() should have set up the key already. */
	if (WARN_ON_ONCE(!ci))
		return -ENOKEY;

	BUILD_BUG_ON(sizeof(ctx) != FSCRYPT_SET_CONTEXT_MAX_SIZE);
	ctxsize = fscrypt_new_context(&ctx, &ci->ci_policy, ci->ci_nonce);

	/*
	 * This may be the first time the inode number is available, so do any
	 * delayed key setup that requires the inode number.
	 */
	if (ci->ci_policy.version == FSCRYPT_POLICY_V2 &&
	    (ci->ci_policy.v2.flags & FSCRYPT_POLICY_FLAG_IV_INO_LBLK_32))
		fscrypt_hash_inode_number(ci, ci->ci_master_key);

	return inode->i_sb->s_cop->set_context(inode, &ctx, ctxsize, fs_data);
}
EXPORT_SYMBOL_GPL(fscrypt_set_context);

/**
 * fscrypt_set_test_dummy_encryption() - handle '-o test_dummy_encryption'
 * @sb: the filesystem on which test_dummy_encryption is being specified
 * @arg: the argument to the test_dummy_encryption option.  May be NULL.
 * @dummy_policy: the filesystem's current dummy policy (input/output, see
 *		  below)
 *
 * Handle the test_dummy_encryption mount option by creating a dummy encryption
 * policy, saving it in @dummy_policy, and adding the corresponding dummy
 * encryption key to the filesystem.  If the @dummy_policy is already set, then
 * instead validate that it matches @arg.  Don't support changing it via
 * remount, as that is difficult to do safely.
 *
 * Return: 0 on success (dummy policy set, or the same policy is already set);
 *         -EEXIST if a different dummy policy is already set;
 *         or another -errno value.
 */
int fscrypt_set_test_dummy_encryption(struct super_block *sb, const char *arg,
				      struct fscrypt_dummy_policy *dummy_policy)
{
	struct fscrypt_key_specifier key_spec = { 0 };
	int version;
	union fscrypt_policy *policy = NULL;
	int err;

	if (!arg)
		arg = "v2";

	if (!strcmp(arg, "v1")) {
		version = FSCRYPT_POLICY_V1;
		key_spec.type = FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR;
		memset(key_spec.u.descriptor, 0x42,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
	} else if (!strcmp(arg, "v2")) {
		version = FSCRYPT_POLICY_V2;
		key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
		/* key_spec.u.identifier gets filled in when adding the key */
	} else {
		err = -EINVAL;
		goto out;
	}

	policy = kzalloc(sizeof(*policy), GFP_KERNEL);
	if (!policy) {
		err = -ENOMEM;
		goto out;
	}

	err = fscrypt_add_test_dummy_key(sb, &key_spec);
	if (err)
		goto out;

	policy->version = version;
	switch (policy->version) {
	case FSCRYPT_POLICY_V1:
		policy->v1.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
		policy->v1.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
		memcpy(policy->v1.master_key_descriptor, key_spec.u.descriptor,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
		break;
	case FSCRYPT_POLICY_V2:
		policy->v2.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
		policy->v2.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
		memcpy(policy->v2.master_key_identifier, key_spec.u.identifier,
		       FSCRYPT_KEY_IDENTIFIER_SIZE);
		break;
	default:
		WARN_ON(1);
		err = -EINVAL;
		goto out;
	}

	if (dummy_policy->policy) {
		if (fscrypt_policies_equal(policy, dummy_policy->policy))
			err = 0;
		else
			err = -EEXIST;
		goto out;
	}
	dummy_policy->policy = policy;
	policy = NULL;
	err = 0;
out:
	kfree(policy);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_set_test_dummy_encryption);

/**
 * fscrypt_show_test_dummy_encryption() - show '-o test_dummy_encryption'
 * @seq: the seq_file to print the option to
 * @sep: the separator character to use
 * @sb: the filesystem whose options are being shown
 *
 * Show the test_dummy_encryption mount option, if it was specified.
 * This is mainly used for /proc/mounts.
 */
void fscrypt_show_test_dummy_encryption(struct seq_file *seq, char sep,
					struct super_block *sb)
{
	const union fscrypt_policy *policy = fscrypt_get_dummy_policy(sb);
	int vers;

	if (!policy)
		return;

	vers = policy->version;
	if (vers == FSCRYPT_POLICY_V1) /* Handle numbering quirk */
		vers = 1;

	seq_printf(seq, "%ctest_dummy_encryption=v%d", sep, vers);
}
EXPORT_SYMBOL_GPL(fscrypt_show_test_dummy_encryption);
