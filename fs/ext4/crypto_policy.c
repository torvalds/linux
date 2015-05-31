/*
 * linux/fs/ext4/crypto_policy.c
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption policy functions for ext4
 *
 * Written by Michael Halcrow, 2015.
 */

#include <linux/random.h>
#include <linux/string.h>
#include <linux/types.h>

#include "ext4.h"
#include "xattr.h"

static int ext4_inode_has_encryption_context(struct inode *inode)
{
	int res = ext4_xattr_get(inode, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, NULL, 0);
	return (res > 0);
}

/*
 * check whether the policy is consistent with the encryption context
 * for the inode
 */
static int ext4_is_encryption_context_consistent_with_policy(
	struct inode *inode, const struct ext4_encryption_policy *policy)
{
	struct ext4_encryption_context ctx;
	int res = ext4_xattr_get(inode, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, &ctx,
				 sizeof(ctx));
	if (res != sizeof(ctx))
		return 0;
	return (memcmp(ctx.master_key_descriptor, policy->master_key_descriptor,
			EXT4_KEY_DESCRIPTOR_SIZE) == 0 &&
		(ctx.flags ==
		 policy->flags) &&
		(ctx.contents_encryption_mode ==
		 policy->contents_encryption_mode) &&
		(ctx.filenames_encryption_mode ==
		 policy->filenames_encryption_mode));
}

static int ext4_create_encryption_context_from_policy(
	struct inode *inode, const struct ext4_encryption_policy *policy)
{
	struct ext4_encryption_context ctx;
	int res = 0;

	res = ext4_convert_inline_data(inode);
	if (res)
		return res;

	ctx.format = EXT4_ENCRYPTION_CONTEXT_FORMAT_V1;
	memcpy(ctx.master_key_descriptor, policy->master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	if (!ext4_valid_contents_enc_mode(policy->contents_encryption_mode)) {
		printk(KERN_WARNING
		       "%s: Invalid contents encryption mode %d\n", __func__,
			policy->contents_encryption_mode);
		return -EINVAL;
	}
	if (!ext4_valid_filenames_enc_mode(policy->filenames_encryption_mode)) {
		printk(KERN_WARNING
		       "%s: Invalid filenames encryption mode %d\n", __func__,
			policy->filenames_encryption_mode);
		return -EINVAL;
	}
	if (policy->flags & ~EXT4_POLICY_FLAGS_VALID)
		return -EINVAL;
	ctx.contents_encryption_mode = policy->contents_encryption_mode;
	ctx.filenames_encryption_mode = policy->filenames_encryption_mode;
	ctx.flags = policy->flags;
	BUILD_BUG_ON(sizeof(ctx.nonce) != EXT4_KEY_DERIVATION_NONCE_SIZE);
	get_random_bytes(ctx.nonce, EXT4_KEY_DERIVATION_NONCE_SIZE);

	res = ext4_xattr_set(inode, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, &ctx,
			     sizeof(ctx), 0);
	if (!res)
		ext4_set_inode_flag(inode, EXT4_INODE_ENCRYPT);
	return res;
}

int ext4_process_policy(const struct ext4_encryption_policy *policy,
			struct inode *inode)
{
	if (policy->version != 0)
		return -EINVAL;

	if (!ext4_inode_has_encryption_context(inode)) {
		if (!ext4_empty_dir(inode))
			return -ENOTEMPTY;
		return ext4_create_encryption_context_from_policy(inode,
								  policy);
	}

	if (ext4_is_encryption_context_consistent_with_policy(inode, policy))
		return 0;

	printk(KERN_WARNING "%s: Policy inconsistent with encryption context\n",
	       __func__);
	return -EINVAL;
}

int ext4_get_policy(struct inode *inode, struct ext4_encryption_policy *policy)
{
	struct ext4_encryption_context ctx;

	int res = ext4_xattr_get(inode, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
				 &ctx, sizeof(ctx));
	if (res != sizeof(ctx))
		return -ENOENT;
	if (ctx.format != EXT4_ENCRYPTION_CONTEXT_FORMAT_V1)
		return -EINVAL;
	policy->version = 0;
	policy->contents_encryption_mode = ctx.contents_encryption_mode;
	policy->filenames_encryption_mode = ctx.filenames_encryption_mode;
	policy->flags = ctx.flags;
	memcpy(&policy->master_key_descriptor, ctx.master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	return 0;
}

int ext4_is_child_context_consistent_with_parent(struct inode *parent,
						 struct inode *child)
{
	struct ext4_crypt_info *parent_ci, *child_ci;
	int res;

	if ((parent == NULL) || (child == NULL)) {
		pr_err("parent %p child %p\n", parent, child);
		BUG_ON(1);
	}
	/* no restrictions if the parent directory is not encrypted */
	if (!ext4_encrypted_inode(parent))
		return 1;
	/* if the child directory is not encrypted, this is always a problem */
	if (!ext4_encrypted_inode(child))
		return 0;
	res = ext4_get_encryption_info(parent);
	if (res)
		return 0;
	res = ext4_get_encryption_info(child);
	if (res)
		return 0;
	parent_ci = EXT4_I(parent)->i_crypt_info;
	child_ci = EXT4_I(child)->i_crypt_info;
	if (!parent_ci && !child_ci)
		return 1;
	if (!parent_ci || !child_ci)
		return 0;

	return (memcmp(parent_ci->ci_master_key,
		       child_ci->ci_master_key,
		       EXT4_KEY_DESCRIPTOR_SIZE) == 0 &&
		(parent_ci->ci_data_mode == child_ci->ci_data_mode) &&
		(parent_ci->ci_filename_mode == child_ci->ci_filename_mode) &&
		(parent_ci->ci_flags == child_ci->ci_flags));
}

/**
 * ext4_inherit_context() - Sets a child context from its parent
 * @parent: Parent inode from which the context is inherited.
 * @child:  Child inode that inherits the context from @parent.
 *
 * Return: Zero on success, non-zero otherwise
 */
int ext4_inherit_context(struct inode *parent, struct inode *child)
{
	struct ext4_encryption_context ctx;
	struct ext4_crypt_info *ci;
	int res;

	res = ext4_get_encryption_info(parent);
	if (res < 0)
		return res;
	ci = EXT4_I(parent)->i_crypt_info;
	BUG_ON(ci == NULL);

	ctx.format = EXT4_ENCRYPTION_CONTEXT_FORMAT_V1;
	if (DUMMY_ENCRYPTION_ENABLED(EXT4_SB(parent->i_sb))) {
		ctx.contents_encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
		ctx.filenames_encryption_mode =
			EXT4_ENCRYPTION_MODE_AES_256_CTS;
		ctx.flags = 0;
		memset(ctx.master_key_descriptor, 0x42,
		       EXT4_KEY_DESCRIPTOR_SIZE);
		res = 0;
	} else {
		ctx.contents_encryption_mode = ci->ci_data_mode;
		ctx.filenames_encryption_mode = ci->ci_filename_mode;
		ctx.flags = ci->ci_flags;
		memcpy(ctx.master_key_descriptor, ci->ci_master_key,
		       EXT4_KEY_DESCRIPTOR_SIZE);
	}
	get_random_bytes(ctx.nonce, EXT4_KEY_DERIVATION_NONCE_SIZE);
	res = ext4_xattr_set(child, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, &ctx,
			     sizeof(ctx), 0);
	if (!res) {
		ext4_set_inode_flag(child, EXT4_INODE_ENCRYPT);
		ext4_clear_inode_state(child, EXT4_STATE_MAY_INLINE_DATA);
		res = ext4_get_encryption_info(child);
	}
	return res;
}
