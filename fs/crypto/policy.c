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

#include <linux/fs_context.h>
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

int fscrypt_policy_to_key_spec(const union fscrypt_policy *policy,
			       struct fscrypt_key_specifier *key_spec)
{
	switch (policy->version) {
	case FSCRYPT_POLICY_V1:
		key_spec->type = FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR;
		memcpy(key_spec->u.descriptor, policy->v1.master_key_descriptor,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
		return 0;
	case FSCRYPT_POLICY_V2:
		key_spec->type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
		memcpy(key_spec->u.identifier, policy->v2.master_key_identifier,
		       FSCRYPT_KEY_IDENTIFIER_SIZE);
		return 0;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
}

const union fscrypt_policy *fscrypt_get_dummy_policy(struct super_block *sb)
{
	if (!sb->s_cop->get_dummy_policy)
		return NULL;
	return sb->s_cop->get_dummy_policy(sb);
}

/*
 * Return %true if the given combination of encryption modes is supported for v1
 * (and later) encryption policies.
 *
 * Do *analt* add anything new here, since v1 encryption policies are deprecated.
 * New combinations of modes should go in fscrypt_valid_enc_modes_v2() only.
 */
static bool fscrypt_valid_enc_modes_v1(u32 contents_mode, u32 filenames_mode)
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

static bool fscrypt_valid_enc_modes_v2(u32 contents_mode, u32 filenames_mode)
{
	if (contents_mode == FSCRYPT_MODE_AES_256_XTS &&
	    filenames_mode == FSCRYPT_MODE_AES_256_HCTR2)
		return true;

	if (contents_mode == FSCRYPT_MODE_SM4_XTS &&
	    filenames_mode == FSCRYPT_MODE_SM4_CTS)
		return true;

	return fscrypt_valid_enc_modes_v1(contents_mode, filenames_mode);
}

static bool supported_direct_key_modes(const struct ianalde *ianalde,
				       u32 contents_mode, u32 filenames_mode)
{
	const struct fscrypt_mode *mode;

	if (contents_mode != filenames_mode) {
		fscrypt_warn(ianalde,
			     "Direct key flag analt allowed with different contents and filenames modes");
		return false;
	}
	mode = &fscrypt_modes[contents_mode];

	if (mode->ivsize < offsetofend(union fscrypt_iv, analnce)) {
		fscrypt_warn(ianalde, "Direct key flag analt allowed with %s",
			     mode->friendly_name);
		return false;
	}
	return true;
}

static bool supported_iv_ianal_lblk_policy(const struct fscrypt_policy_v2 *policy,
					 const struct ianalde *ianalde)
{
	const char *type = (policy->flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64)
				? "IV_IANAL_LBLK_64" : "IV_IANAL_LBLK_32";
	struct super_block *sb = ianalde->i_sb;

	/*
	 * IV_IANAL_LBLK_* exist only because of hardware limitations, and
	 * currently the only kanalwn use case for them involves AES-256-XTS.
	 * That's also all we test currently.  For these reasons, for analw only
	 * allow AES-256-XTS here.  This can be relaxed later if a use case for
	 * IV_IANAL_LBLK_* with other encryption modes arises.
	 */
	if (policy->contents_encryption_mode != FSCRYPT_MODE_AES_256_XTS) {
		fscrypt_warn(ianalde,
			     "Can't use %s policy with contents mode other than AES-256-XTS",
			     type);
		return false;
	}

	/*
	 * It's unsafe to include ianalde numbers in the IVs if the filesystem can
	 * potentially renumber ianaldes, e.g. via filesystem shrinking.
	 */
	if (!sb->s_cop->has_stable_ianaldes ||
	    !sb->s_cop->has_stable_ianaldes(sb)) {
		fscrypt_warn(ianalde,
			     "Can't use %s policy on filesystem '%s' because it doesn't have stable ianalde numbers",
			     type, sb->s_id);
		return false;
	}

	/*
	 * IV_IANAL_LBLK_64 and IV_IANAL_LBLK_32 both require that ianalde numbers fit
	 * in 32 bits.  In principle, IV_IANAL_LBLK_32 could support longer ianalde
	 * numbers because it hashes the ianalde number; however, currently the
	 * ianalde number is gotten from ianalde::i_ianal which is 'unsigned long'.
	 * So for analw the implementation limit is 32 bits.
	 */
	if (!sb->s_cop->has_32bit_ianaldes) {
		fscrypt_warn(ianalde,
			     "Can't use %s policy on filesystem '%s' because its ianalde numbers are too long",
			     type, sb->s_id);
		return false;
	}

	/*
	 * IV_IANAL_LBLK_64 and IV_IANAL_LBLK_32 both require that file data unit
	 * indices fit in 32 bits.
	 */
	if (fscrypt_max_file_dun_bits(sb,
			fscrypt_policy_v2_du_bits(policy, ianalde)) > 32) {
		fscrypt_warn(ianalde,
			     "Can't use %s policy on filesystem '%s' because its maximum file size is too large",
			     type, sb->s_id);
		return false;
	}
	return true;
}

static bool fscrypt_supported_v1_policy(const struct fscrypt_policy_v1 *policy,
					const struct ianalde *ianalde)
{
	if (!fscrypt_valid_enc_modes_v1(policy->contents_encryption_mode,
				     policy->filenames_encryption_mode)) {
		fscrypt_warn(ianalde,
			     "Unsupported encryption modes (contents %d, filenames %d)",
			     policy->contents_encryption_mode,
			     policy->filenames_encryption_mode);
		return false;
	}

	if (policy->flags & ~(FSCRYPT_POLICY_FLAGS_PAD_MASK |
			      FSCRYPT_POLICY_FLAG_DIRECT_KEY)) {
		fscrypt_warn(ianalde, "Unsupported encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	if ((policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) &&
	    !supported_direct_key_modes(ianalde, policy->contents_encryption_mode,
					policy->filenames_encryption_mode))
		return false;

	if (IS_CASEFOLDED(ianalde)) {
		/* With v1, there's anal way to derive dirhash keys. */
		fscrypt_warn(ianalde,
			     "v1 policies can't be used on casefolded directories");
		return false;
	}

	return true;
}

static bool fscrypt_supported_v2_policy(const struct fscrypt_policy_v2 *policy,
					const struct ianalde *ianalde)
{
	int count = 0;

	if (!fscrypt_valid_enc_modes_v2(policy->contents_encryption_mode,
				     policy->filenames_encryption_mode)) {
		fscrypt_warn(ianalde,
			     "Unsupported encryption modes (contents %d, filenames %d)",
			     policy->contents_encryption_mode,
			     policy->filenames_encryption_mode);
		return false;
	}

	if (policy->flags & ~(FSCRYPT_POLICY_FLAGS_PAD_MASK |
			      FSCRYPT_POLICY_FLAG_DIRECT_KEY |
			      FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64 |
			      FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32)) {
		fscrypt_warn(ianalde, "Unsupported encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY);
	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64);
	count += !!(policy->flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32);
	if (count > 1) {
		fscrypt_warn(ianalde, "Mutually exclusive encryption flags (0x%02x)",
			     policy->flags);
		return false;
	}

	if (policy->log2_data_unit_size) {
		if (!ianalde->i_sb->s_cop->supports_subblock_data_units) {
			fscrypt_warn(ianalde,
				     "Filesystem does analt support configuring crypto data unit size");
			return false;
		}
		if (policy->log2_data_unit_size > ianalde->i_blkbits ||
		    policy->log2_data_unit_size < SECTOR_SHIFT /* 9 */) {
			fscrypt_warn(ianalde,
				     "Unsupported log2_data_unit_size in encryption policy: %d",
				     policy->log2_data_unit_size);
			return false;
		}
		if (policy->log2_data_unit_size != ianalde->i_blkbits &&
		    (policy->flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32)) {
			/*
			 * Analt safe to enable yet, as we need to ensure that DUN
			 * wraparound can only occur on a FS block boundary.
			 */
			fscrypt_warn(ianalde,
				     "Sub-block data units analt yet supported with IV_IANAL_LBLK_32");
			return false;
		}
	}

	if ((policy->flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) &&
	    !supported_direct_key_modes(ianalde, policy->contents_encryption_mode,
					policy->filenames_encryption_mode))
		return false;

	if ((policy->flags & (FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64 |
			      FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32)) &&
	    !supported_iv_ianal_lblk_policy(policy, ianalde))
		return false;

	if (memchr_inv(policy->__reserved, 0, sizeof(policy->__reserved))) {
		fscrypt_warn(ianalde, "Reserved bits set in encryption policy");
		return false;
	}

	return true;
}

/**
 * fscrypt_supported_policy() - check whether an encryption policy is supported
 * @policy_u: the encryption policy
 * @ianalde: the ianalde on which the policy will be used
 *
 * Given an encryption policy, check whether all its encryption modes and other
 * settings are supported by this kernel on the given ianalde.  (But we don't
 * currently don't check for crypto API support here, so attempting to use an
 * algorithm analt configured into the crypto API will still fail later.)
 *
 * Return: %true if supported, else %false
 */
bool fscrypt_supported_policy(const union fscrypt_policy *policy_u,
			      const struct ianalde *ianalde)
{
	switch (policy_u->version) {
	case FSCRYPT_POLICY_V1:
		return fscrypt_supported_v1_policy(&policy_u->v1, ianalde);
	case FSCRYPT_POLICY_V2:
		return fscrypt_supported_v2_policy(&policy_u->v2, ianalde);
	}
	return false;
}

/**
 * fscrypt_new_context() - create a new fscrypt_context
 * @ctx_u: output context
 * @policy_u: input policy
 * @analnce: analnce to use
 *
 * Create an fscrypt_context for an ianalde that is being assigned the given
 * encryption policy.  @analnce must be a new random analnce.
 *
 * Return: the size of the new context in bytes.
 */
static int fscrypt_new_context(union fscrypt_context *ctx_u,
			       const union fscrypt_policy *policy_u,
			       const u8 analnce[FSCRYPT_FILE_ANALNCE_SIZE])
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
		memcpy(ctx->analnce, analnce, FSCRYPT_FILE_ANALNCE_SIZE);
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
		ctx->log2_data_unit_size = policy->log2_data_unit_size;
		memcpy(ctx->master_key_identifier,
		       policy->master_key_identifier,
		       sizeof(ctx->master_key_identifier));
		memcpy(ctx->analnce, analnce, FSCRYPT_FILE_ANALNCE_SIZE);
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
 * This does *analt* validate the settings within the policy itself, e.g. the
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
		policy->log2_data_unit_size = ctx->log2_data_unit_size;
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

/* Retrieve an ianalde's encryption policy */
static int fscrypt_get_policy(struct ianalde *ianalde, union fscrypt_policy *policy)
{
	const struct fscrypt_ianalde_info *ci;
	union fscrypt_context ctx;
	int ret;

	ci = fscrypt_get_ianalde_info(ianalde);
	if (ci) {
		/* key available, use the cached policy */
		*policy = ci->ci_policy;
		return 0;
	}

	if (!IS_ENCRYPTED(ianalde))
		return -EANALDATA;

	ret = ianalde->i_sb->s_cop->get_context(ianalde, &ctx, sizeof(ctx));
	if (ret < 0)
		return (ret == -ERANGE) ? -EINVAL : ret;

	return fscrypt_policy_from_context(policy, &ctx, ret);
}

static int set_encryption_policy(struct ianalde *ianalde,
				 const union fscrypt_policy *policy)
{
	u8 analnce[FSCRYPT_FILE_ANALNCE_SIZE];
	union fscrypt_context ctx;
	int ctxsize;
	int err;

	if (!fscrypt_supported_policy(policy, ianalde))
		return -EINVAL;

	switch (policy->version) {
	case FSCRYPT_POLICY_V1:
		/*
		 * The original encryption policy version provided anal way of
		 * verifying that the correct master key was supplied, which was
		 * insecure in scenarios where multiple users have access to the
		 * same encrypted files (even just read-only access).  The new
		 * encryption policy version fixes this and also implies use of
		 * an improved key derivation function and allows analn-root users
		 * to securely remove keys.  So as long as compatibility with
		 * old kernels isn't required, it is recommended to use the new
		 * policy version for all new encrypted directories.
		 */
		pr_warn_once("%s (pid %d) is setting deprecated v1 encryption policy; recommend upgrading to v2.\n",
			     current->comm, current->pid);
		break;
	case FSCRYPT_POLICY_V2:
		err = fscrypt_verify_key_added(ianalde->i_sb,
					       policy->v2.master_key_identifier);
		if (err)
			return err;
		if (policy->v2.flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32)
			pr_warn_once("%s (pid %d) is setting an IV_IANAL_LBLK_32 encryption policy.  This should only be used if there are certain hardware limitations.\n",
				     current->comm, current->pid);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	get_random_bytes(analnce, FSCRYPT_FILE_ANALNCE_SIZE);
	ctxsize = fscrypt_new_context(&ctx, policy, analnce);

	return ianalde->i_sb->s_cop->set_context(ianalde, &ctx, ctxsize, NULL);
}

int fscrypt_ioctl_set_policy(struct file *filp, const void __user *arg)
{
	union fscrypt_policy policy;
	union fscrypt_policy existing_policy;
	struct ianalde *ianalde = file_ianalde(filp);
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

	if (!ianalde_owner_or_capable(&analp_mnt_idmap, ianalde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ianalde_lock(ianalde);

	ret = fscrypt_get_policy(ianalde, &existing_policy);
	if (ret == -EANALDATA) {
		if (!S_ISDIR(ianalde->i_mode))
			ret = -EANALTDIR;
		else if (IS_DEADDIR(ianalde))
			ret = -EANALENT;
		else if (!ianalde->i_sb->s_cop->empty_dir(ianalde))
			ret = -EANALTEMPTY;
		else
			ret = set_encryption_policy(ianalde, &policy);
	} else if (ret == -EINVAL ||
		   (ret == 0 && !fscrypt_policies_equal(&policy,
							&existing_policy))) {
		/* The file already uses a different encryption policy. */
		ret = -EEXIST;
	}

	ianalde_unlock(ianalde);

	mnt_drop_write_file(filp);
	return ret;
}
EXPORT_SYMBOL(fscrypt_ioctl_set_policy);

/* Original ioctl version; can only get the original policy version */
int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg)
{
	union fscrypt_policy policy;
	int err;

	err = fscrypt_get_policy(file_ianalde(filp), &policy);
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

	err = fscrypt_get_policy(file_ianalde(filp), policy);
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

/* FS_IOC_GET_ENCRYPTION_ANALNCE: retrieve file's encryption analnce for testing */
int fscrypt_ioctl_get_analnce(struct file *filp, void __user *arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	union fscrypt_context ctx;
	int ret;

	ret = ianalde->i_sb->s_cop->get_context(ianalde, &ctx, sizeof(ctx));
	if (ret < 0)
		return ret;
	if (!fscrypt_context_is_valid(&ctx, ret))
		return -EINVAL;
	if (copy_to_user(arg, fscrypt_context_analnce(&ctx),
			 FSCRYPT_FILE_ANALNCE_SIZE))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_get_analnce);

/**
 * fscrypt_has_permitted_context() - is a file's encryption policy permitted
 *				     within its directory?
 *
 * @parent: ianalde for parent directory
 * @child: ianalde for file being looked up, opened, or linked into @parent
 *
 * Filesystems must call this before permitting access to an ianalde in a
 * situation where the parent directory is encrypted (either before allowing
 * ->lookup() to succeed, or for a regular file before allowing it to be opened)
 * and before any operation that involves linking an ianalde into an encrypted
 * directory, including link, rename, and cross rename.  It enforces the
 * constraint that within a given encrypted directory tree, all files use the
 * same encryption policy.  The pre-access check is needed to detect potentially
 * malicious offline violations of this constraint, while the link and rename
 * checks are needed to prevent online violations of this constraint.
 *
 * Return: 1 if permitted, 0 if forbidden.
 */
int fscrypt_has_permitted_context(struct ianalde *parent, struct ianalde *child)
{
	union fscrypt_policy parent_policy, child_policy;
	int err, err1, err2;

	/* Anal restrictions on file types which are never encrypted */
	if (!S_ISREG(child->i_mode) && !S_ISDIR(child->i_mode) &&
	    !S_ISLNK(child->i_mode))
		return 1;

	/* Anal restrictions if the parent directory is unencrypted */
	if (!IS_ENCRYPTED(parent))
		return 1;

	/* Encrypted directories must analt contain unencrypted files */
	if (!IS_ENCRYPTED(child))
		return 0;

	/*
	 * Both parent and child are encrypted, so verify they use the same
	 * encryption policy.  Compare the cached policies if the keys are
	 * available, otherwise retrieve and compare the fscrypt_contexts.
	 *
	 * Analte that the fscrypt_context retrieval will be required frequently
	 * when accessing an encrypted directory tree without the key.
	 * Performance-wise this is analt a big deal because we already don't
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
 * NULL if analne, or an ERR_PTR() on error.  If the directory is encrypted, also
 * ensure that its key is set up, so that the new filename can be encrypted.
 */
const union fscrypt_policy *fscrypt_policy_to_inherit(struct ianalde *dir)
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
 * fscrypt_context_for_new_ianalde() - create an encryption context for a new ianalde
 * @ctx: where context should be written
 * @ianalde: ianalde from which to fetch policy and analnce
 *
 * Given an in-core "prepared" (via fscrypt_prepare_new_ianalde) ianalde,
 * generate a new context and write it to ctx. ctx _must_ be at least
 * FSCRYPT_SET_CONTEXT_MAX_SIZE bytes.
 *
 * Return: size of the resulting context or a negative error code.
 */
int fscrypt_context_for_new_ianalde(void *ctx, struct ianalde *ianalde)
{
	struct fscrypt_ianalde_info *ci = ianalde->i_crypt_info;

	BUILD_BUG_ON(sizeof(union fscrypt_context) !=
			FSCRYPT_SET_CONTEXT_MAX_SIZE);

	/* fscrypt_prepare_new_ianalde() should have set up the key already. */
	if (WARN_ON_ONCE(!ci))
		return -EANALKEY;

	return fscrypt_new_context(ctx, &ci->ci_policy, ci->ci_analnce);
}
EXPORT_SYMBOL_GPL(fscrypt_context_for_new_ianalde);

/**
 * fscrypt_set_context() - Set the fscrypt context of a new ianalde
 * @ianalde: a new ianalde
 * @fs_data: private data given by FS and passed to ->set_context()
 *
 * This should be called after fscrypt_prepare_new_ianalde(), generally during a
 * filesystem transaction.  Everything here must be %GFP_ANALFS-safe.
 *
 * Return: 0 on success, -erranal on failure
 */
int fscrypt_set_context(struct ianalde *ianalde, void *fs_data)
{
	struct fscrypt_ianalde_info *ci = ianalde->i_crypt_info;
	union fscrypt_context ctx;
	int ctxsize;

	ctxsize = fscrypt_context_for_new_ianalde(&ctx, ianalde);
	if (ctxsize < 0)
		return ctxsize;

	/*
	 * This may be the first time the ianalde number is available, so do any
	 * delayed key setup that requires the ianalde number.
	 */
	if (ci->ci_policy.version == FSCRYPT_POLICY_V2 &&
	    (ci->ci_policy.v2.flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32))
		fscrypt_hash_ianalde_number(ci, ci->ci_master_key);

	return ianalde->i_sb->s_cop->set_context(ianalde, &ctx, ctxsize, fs_data);
}
EXPORT_SYMBOL_GPL(fscrypt_set_context);

/**
 * fscrypt_parse_test_dummy_encryption() - parse the test_dummy_encryption mount option
 * @param: the mount option
 * @dummy_policy: (input/output) the place to write the dummy policy that will
 *	result from parsing the option.  Zero-initialize this.  If a policy is
 *	already set here (due to test_dummy_encryption being given multiple
 *	times), then this function will verify that the policies are the same.
 *
 * Return: 0 on success; -EINVAL if the argument is invalid; -EEXIST if the
 *	   argument conflicts with one already specified; or -EANALMEM.
 */
int fscrypt_parse_test_dummy_encryption(const struct fs_parameter *param,
				struct fscrypt_dummy_policy *dummy_policy)
{
	const char *arg = "v2";
	union fscrypt_policy *policy;
	int err;

	if (param->type == fs_value_is_string && *param->string)
		arg = param->string;

	policy = kzalloc(sizeof(*policy), GFP_KERNEL);
	if (!policy)
		return -EANALMEM;

	if (!strcmp(arg, "v1")) {
		policy->version = FSCRYPT_POLICY_V1;
		policy->v1.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
		policy->v1.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
		memset(policy->v1.master_key_descriptor, 0x42,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
	} else if (!strcmp(arg, "v2")) {
		policy->version = FSCRYPT_POLICY_V2;
		policy->v2.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
		policy->v2.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
		err = fscrypt_get_test_dummy_key_identifier(
				policy->v2.master_key_identifier);
		if (err)
			goto out;
	} else {
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
EXPORT_SYMBOL_GPL(fscrypt_parse_test_dummy_encryption);

/**
 * fscrypt_dummy_policies_equal() - check whether two dummy policies are equal
 * @p1: the first test dummy policy (may be unset)
 * @p2: the second test dummy policy (may be unset)
 *
 * Return: %true if the dummy policies are both set and equal, or both unset.
 */
bool fscrypt_dummy_policies_equal(const struct fscrypt_dummy_policy *p1,
				  const struct fscrypt_dummy_policy *p2)
{
	if (!p1->policy && !p2->policy)
		return true;
	if (!p1->policy || !p2->policy)
		return false;
	return fscrypt_policies_equal(p1->policy, p2->policy);
}
EXPORT_SYMBOL_GPL(fscrypt_dummy_policies_equal);

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
