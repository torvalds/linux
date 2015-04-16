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

	ctx.format = EXT4_ENCRYPTION_CONTEXT_FORMAT_V1;
	memcpy(ctx.master_key_descriptor, policy->master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	if (!ext4_valid_contents_enc_mode(policy->contents_encryption_mode)) {
		printk(KERN_WARNING
		       "%s: Invalid contents encryption mode %d\n", __func__,
			policy->contents_encryption_mode);
		res = -EINVAL;
		goto out;
	}
	if (!ext4_valid_filenames_enc_mode(policy->filenames_encryption_mode)) {
		printk(KERN_WARNING
		       "%s: Invalid filenames encryption mode %d\n", __func__,
			policy->filenames_encryption_mode);
		res = -EINVAL;
		goto out;
	}
	ctx.contents_encryption_mode = policy->contents_encryption_mode;
	ctx.filenames_encryption_mode = policy->filenames_encryption_mode;
	BUILD_BUG_ON(sizeof(ctx.nonce) != EXT4_KEY_DERIVATION_NONCE_SIZE);
	get_random_bytes(ctx.nonce, EXT4_KEY_DERIVATION_NONCE_SIZE);

	res = ext4_xattr_set(inode, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, &ctx,
			     sizeof(ctx), 0);
out:
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
	memcpy(&policy->master_key_descriptor, ctx.master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	return 0;
}

int ext4_is_child_context_consistent_with_parent(struct inode *parent,
						 struct inode *child)
{
	struct ext4_encryption_context parent_ctx, child_ctx;
	int res;

	if ((parent == NULL) || (child == NULL)) {
		pr_err("parent %p child %p\n", parent, child);
		BUG_ON(1);
	}
	/* no restrictions if the parent directory is not encrypted */
	if (!ext4_encrypted_inode(parent))
		return 1;
	res = ext4_xattr_get(parent, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
			     &parent_ctx, sizeof(parent_ctx));
	if (res != sizeof(parent_ctx))
		return 0;
	/* if the child directory is not encrypted, this is always a problem */
	if (!ext4_encrypted_inode(child))
		return 0;
	res = ext4_xattr_get(child, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
			     &child_ctx, sizeof(child_ctx));
	if (res != sizeof(child_ctx))
		return 0;
	return (memcmp(parent_ctx.master_key_descriptor,
		       child_ctx.master_key_descriptor,
		       EXT4_KEY_DESCRIPTOR_SIZE) == 0 &&
		(parent_ctx.contents_encryption_mode ==
		 child_ctx.contents_encryption_mode) &&
		(parent_ctx.filenames_encryption_mode ==
		 child_ctx.filenames_encryption_mode));
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
	int res = ext4_xattr_get(parent, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
				 &ctx, sizeof(ctx));

	if (res != sizeof(ctx)) {
		if (DUMMY_ENCRYPTION_ENABLED(EXT4_SB(parent->i_sb))) {
			ctx.format = EXT4_ENCRYPTION_CONTEXT_FORMAT_V1;
			ctx.contents_encryption_mode =
				EXT4_ENCRYPTION_MODE_AES_256_XTS;
			ctx.filenames_encryption_mode =
				EXT4_ENCRYPTION_MODE_AES_256_CTS;
			memset(ctx.master_key_descriptor, 0x42,
			       EXT4_KEY_DESCRIPTOR_SIZE);
			res = 0;
		} else {
			goto out;
		}
	}
	get_random_bytes(ctx.nonce, EXT4_KEY_DERIVATION_NONCE_SIZE);
	res = ext4_xattr_set(child, EXT4_XATTR_INDEX_ENCRYPTION,
			     EXT4_XATTR_NAME_ENCRYPTION_CONTEXT, &ctx,
			     sizeof(ctx), 0);
out:
	if (!res)
		ext4_set_inode_flag(child, EXT4_INODE_ENCRYPT);
	return res;
}
