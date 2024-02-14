// SPDX-License-Identifier: GPL-2.0
#include "ubifs.h"

static int ubifs_crypt_get_context(struct inode *inode, void *ctx, size_t len)
{
	return ubifs_xattr_get(inode, UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT,
			       ctx, len);
}

static int ubifs_crypt_set_context(struct inode *inode, const void *ctx,
				   size_t len, void *fs_data)
{
	/*
	 * Creating an encryption context is done unlocked since we
	 * operate on a new inode which is not visible to other users
	 * at this point. So, no need to check whether inode is locked.
	 */
	return ubifs_xattr_set(inode, UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT,
			       ctx, len, 0, false);
}

static bool ubifs_crypt_empty_dir(struct inode *inode)
{
	return ubifs_check_dir_empty(inode) == 0;
}

/**
 * ubifs_encrypt - Encrypt data.
 * @inode: inode which refers to the data node
 * @dn: data node to encrypt
 * @in_len: length of data to be compressed
 * @out_len: allocated memory size for the data area of @dn
 * @block: logical block number of the block
 *
 * This function encrypt a possibly-compressed data in the data node.
 * The encrypted data length will store in @out_len.
 */
int ubifs_encrypt(const struct inode *inode, struct ubifs_data_node *dn,
		  unsigned int in_len, unsigned int *out_len, int block)
{
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	void *p = &dn->data;
	unsigned int pad_len = round_up(in_len, UBIFS_CIPHER_BLOCK_SIZE);
	int err;

	ubifs_assert(c, pad_len <= *out_len);
	dn->compr_size = cpu_to_le16(in_len);

	/* pad to full block cipher length */
	if (pad_len != in_len)
		memset(p + in_len, 0, pad_len - in_len);

	err = fscrypt_encrypt_block_inplace(inode, virt_to_page(p), pad_len,
					    offset_in_page(p), block, GFP_NOFS);
	if (err) {
		ubifs_err(c, "fscrypt_encrypt_block_inplace() failed: %d", err);
		return err;
	}
	*out_len = pad_len;

	return 0;
}

int ubifs_decrypt(const struct inode *inode, struct ubifs_data_node *dn,
		  unsigned int *out_len, int block)
{
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	int err;
	unsigned int clen = le16_to_cpu(dn->compr_size);
	unsigned int dlen = *out_len;

	if (clen <= 0 || clen > UBIFS_BLOCK_SIZE || clen > dlen) {
		ubifs_err(c, "bad compr_size: %i", clen);
		return -EINVAL;
	}

	ubifs_assert(c, dlen <= UBIFS_BLOCK_SIZE);
	err = fscrypt_decrypt_block_inplace(inode, virt_to_page(&dn->data),
					    dlen, offset_in_page(&dn->data),
					    block);
	if (err) {
		ubifs_err(c, "fscrypt_decrypt_block_inplace() failed: %d", err);
		return err;
	}
	*out_len = clen;

	return 0;
}

const struct fscrypt_operations ubifs_crypt_operations = {
	.legacy_key_prefix	= "ubifs:",
	.get_context		= ubifs_crypt_get_context,
	.set_context		= ubifs_crypt_set_context,
	.empty_dir		= ubifs_crypt_empty_dir,
};
