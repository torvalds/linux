/*
 * linux/fs/f2fs/crypto_fname.c
 *
 * Copied from linux/fs/ext4/crypto.c
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * This contains functions for filename crypto management in f2fs
 *
 * Written by Uday Savagaonkar, 2014.
 *
 * Adjust f2fs dentry structure
 *	Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 */
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <keys/encrypted-type.h>
#include <keys/user-type.h>
#include <linux/crypto.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/key.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/spinlock_types.h>
#include <linux/f2fs_fs.h>
#include <linux/ratelimit.h>

#include "f2fs.h"
#include "f2fs_crypto.h"
#include "xattr.h"

/**
 * f2fs_dir_crypt_complete() -
 */
static void f2fs_dir_crypt_complete(struct crypto_async_request *req, int res)
{
	struct f2fs_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

bool f2fs_valid_filenames_enc_mode(uint32_t mode)
{
	return (mode == F2FS_ENCRYPTION_MODE_AES_256_CTS);
}

static unsigned max_name_len(struct inode *inode)
{
	return S_ISLNK(inode->i_mode) ? inode->i_sb->s_blocksize :
					F2FS_NAME_LEN;
}

/**
 * f2fs_fname_encrypt() -
 *
 * This function encrypts the input filename, and returns the length of the
 * ciphertext. Errors are returned as negative numbers.  We trust the caller to
 * allocate sufficient memory to oname string.
 */
static int f2fs_fname_encrypt(struct inode *inode,
			const struct qstr *iname, struct f2fs_str *oname)
{
	u32 ciphertext_len;
	struct ablkcipher_request *req = NULL;
	DECLARE_F2FS_COMPLETION_RESULT(ecr);
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;
	struct crypto_ablkcipher *tfm = ci->ci_ctfm;
	int res = 0;
	char iv[F2FS_CRYPTO_BLOCK_SIZE];
	struct scatterlist src_sg, dst_sg;
	int padding = 4 << (ci->ci_flags & F2FS_POLICY_FLAGS_PAD_MASK);
	char *workbuf, buf[32], *alloc_buf = NULL;
	unsigned lim = max_name_len(inode);

	if (iname->len <= 0 || iname->len > lim)
		return -EIO;

	ciphertext_len = (iname->len < F2FS_CRYPTO_BLOCK_SIZE) ?
		F2FS_CRYPTO_BLOCK_SIZE : iname->len;
	ciphertext_len = f2fs_fname_crypto_round_up(ciphertext_len, padding);
	ciphertext_len = (ciphertext_len > lim) ? lim : ciphertext_len;

	if (ciphertext_len <= sizeof(buf)) {
		workbuf = buf;
	} else {
		alloc_buf = kmalloc(ciphertext_len, GFP_NOFS);
		if (!alloc_buf)
			return -ENOMEM;
		workbuf = alloc_buf;
	}

	/* Allocate request */
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
			"%s: crypto_request_alloc() failed\n", __func__);
		kfree(alloc_buf);
		return -ENOMEM;
	}
	ablkcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			f2fs_dir_crypt_complete, &ecr);

	/* Copy the input */
	memcpy(workbuf, iname->name, iname->len);
	if (iname->len < ciphertext_len)
		memset(workbuf + iname->len, 0, ciphertext_len - iname->len);

	/* Initialize IV */
	memset(iv, 0, F2FS_CRYPTO_BLOCK_SIZE);

	/* Create encryption request */
	sg_init_one(&src_sg, workbuf, ciphertext_len);
	sg_init_one(&dst_sg, oname->name, ciphertext_len);
	ablkcipher_request_set_crypt(req, &src_sg, &dst_sg, ciphertext_len, iv);
	res = crypto_ablkcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	kfree(alloc_buf);
	ablkcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(KERN_ERR
				"%s: Error (error code %d)\n", __func__, res);
	}
	oname->len = ciphertext_len;
	return res;
}

/*
 * f2fs_fname_decrypt()
 *	This function decrypts the input filename, and returns
 *	the length of the plaintext.
 *	Errors are returned as negative numbers.
 *	We trust the caller to allocate sufficient memory to oname string.
 */
static int f2fs_fname_decrypt(struct inode *inode,
			const struct f2fs_str *iname, struct f2fs_str *oname)
{
	struct ablkcipher_request *req = NULL;
	DECLARE_F2FS_COMPLETION_RESULT(ecr);
	struct scatterlist src_sg, dst_sg;
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;
	struct crypto_ablkcipher *tfm = ci->ci_ctfm;
	int res = 0;
	char iv[F2FS_CRYPTO_BLOCK_SIZE];
	unsigned lim = max_name_len(inode);

	if (iname->len <= 0 || iname->len > lim)
		return -EIO;

	/* Allocate request */
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
			"%s: crypto_request_alloc() failed\n",  __func__);
		return -ENOMEM;
	}
	ablkcipher_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		f2fs_dir_crypt_complete, &ecr);

	/* Initialize IV */
	memset(iv, 0, F2FS_CRYPTO_BLOCK_SIZE);

	/* Create decryption request */
	sg_init_one(&src_sg, iname->name, iname->len);
	sg_init_one(&dst_sg, oname->name, oname->len);
	ablkcipher_request_set_crypt(req, &src_sg, &dst_sg, iname->len, iv);
	res = crypto_ablkcipher_decrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	ablkcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(KERN_ERR
			"%s: Error in f2fs_fname_decrypt (error code %d)\n",
			__func__, res);
		return res;
	}

	oname->len = strnlen(oname->name, iname->len);
	return oname->len;
}

static const char *lookup_table =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

/**
 * f2fs_fname_encode_digest() -
 *
 * Encodes the input digest using characters from the set [a-zA-Z0-9_+].
 * The encoded string is roughly 4/3 times the size of the input string.
 */
static int digest_encode(const char *src, int len, char *dst)
{
	int i = 0, bits = 0, ac = 0;
	char *cp = dst;

	while (i < len) {
		ac += (((unsigned char) src[i]) << bits);
		bits += 8;
		do {
			*cp++ = lookup_table[ac & 0x3f];
			ac >>= 6;
			bits -= 6;
		} while (bits >= 6);
		i++;
	}
	if (bits)
		*cp++ = lookup_table[ac & 0x3f];
	return cp - dst;
}

static int digest_decode(const char *src, int len, char *dst)
{
	int i = 0, bits = 0, ac = 0;
	const char *p;
	char *cp = dst;

	while (i < len) {
		p = strchr(lookup_table, src[i]);
		if (p == NULL || src[i] == 0)
			return -2;
		ac += (p - lookup_table) << bits;
		bits += 6;
		if (bits >= 8) {
			*cp++ = ac & 0xff;
			ac >>= 8;
			bits -= 8;
		}
		i++;
	}
	if (ac)
		return -1;
	return cp - dst;
}

int f2fs_setup_fname_crypto(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_crypt_info *ci = fi->i_crypt_info;
	struct crypto_ablkcipher *ctfm;
	int res;

	/* Check if the crypto policy is set on the inode */
	res = f2fs_encrypted_inode(inode);
	if (res == 0)
		return 0;

	res = f2fs_get_encryption_info(inode);
	if (res < 0)
		return res;
	ci = fi->i_crypt_info;

	if (!ci || ci->ci_ctfm)
		return 0;

	if (ci->ci_filename_mode != F2FS_ENCRYPTION_MODE_AES_256_CTS) {
		printk_once(KERN_WARNING "f2fs: unsupported key mode %d\n",
				ci->ci_filename_mode);
		return -ENOKEY;
	}

	ctfm = crypto_alloc_ablkcipher("cts(cbc(aes))", 0, 0);
	if (!ctfm || IS_ERR(ctfm)) {
		res = ctfm ? PTR_ERR(ctfm) : -ENOMEM;
		printk(KERN_DEBUG "%s: error (%d) allocating crypto tfm\n",
				__func__, res);
		return res;
	}
	crypto_ablkcipher_clear_flags(ctfm, ~0);
	crypto_tfm_set_flags(crypto_ablkcipher_tfm(ctfm),
			     CRYPTO_TFM_REQ_WEAK_KEY);

	res = crypto_ablkcipher_setkey(ctfm, ci->ci_raw, ci->ci_size);
	if (res) {
		crypto_free_ablkcipher(ctfm);
		return -EIO;
	}
	ci->ci_ctfm = ctfm;
	return 0;
}

/**
 * f2fs_fname_crypto_round_up() -
 *
 * Return: The next multiple of block size
 */
u32 f2fs_fname_crypto_round_up(u32 size, u32 blksize)
{
	return ((size + blksize - 1) / blksize) * blksize;
}

/**
 * f2fs_fname_crypto_alloc_obuff() -
 *
 * Allocates an output buffer that is sufficient for the crypto operation
 * specified by the context and the direction.
 */
int f2fs_fname_crypto_alloc_buffer(struct inode *inode,
				   u32 ilen, struct f2fs_str *crypto_str)
{
	unsigned int olen;
	int padding = 16;
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;

	if (ci)
		padding = 4 << (ci->ci_flags & F2FS_POLICY_FLAGS_PAD_MASK);
	if (padding < F2FS_CRYPTO_BLOCK_SIZE)
		padding = F2FS_CRYPTO_BLOCK_SIZE;
	olen = f2fs_fname_crypto_round_up(ilen, padding);
	crypto_str->len = olen;
	if (olen < F2FS_FNAME_CRYPTO_DIGEST_SIZE * 2)
		olen = F2FS_FNAME_CRYPTO_DIGEST_SIZE * 2;
	/* Allocated buffer can hold one more character to null-terminate the
	 * string */
	crypto_str->name = kmalloc(olen + 1, GFP_NOFS);
	if (!(crypto_str->name))
		return -ENOMEM;
	return 0;
}

/**
 * f2fs_fname_crypto_free_buffer() -
 *
 * Frees the buffer allocated for crypto operation.
 */
void f2fs_fname_crypto_free_buffer(struct f2fs_str *crypto_str)
{
	if (!crypto_str)
		return;
	kfree(crypto_str->name);
	crypto_str->name = NULL;
}

/**
 * f2fs_fname_disk_to_usr() - converts a filename from disk space to user space
 */
int f2fs_fname_disk_to_usr(struct inode *inode,
			f2fs_hash_t *hash,
			const struct f2fs_str *iname,
			struct f2fs_str *oname)
{
	const struct qstr qname = FSTR_TO_QSTR(iname);
	char buf[24];
	int ret;

	if (is_dot_dotdot(&qname)) {
		oname->name[0] = '.';
		oname->name[iname->len - 1] = '.';
		oname->len = iname->len;
		return oname->len;
	}

	if (F2FS_I(inode)->i_crypt_info)
		return f2fs_fname_decrypt(inode, iname, oname);

	if (iname->len <= F2FS_FNAME_CRYPTO_DIGEST_SIZE) {
		ret = digest_encode(iname->name, iname->len, oname->name);
		oname->len = ret;
		return ret;
	}
	if (hash) {
		memcpy(buf, hash, 4);
		memset(buf + 4, 0, 4);
	} else
		memset(buf, 0, 8);
	memcpy(buf + 8, iname->name + iname->len - 16, 16);
	oname->name[0] = '_';
	ret = digest_encode(buf, 24, oname->name + 1);
	oname->len = ret + 1;
	return ret + 1;
}

/**
 * f2fs_fname_usr_to_disk() - converts a filename from user space to disk space
 */
int f2fs_fname_usr_to_disk(struct inode *inode,
			const struct qstr *iname,
			struct f2fs_str *oname)
{
	int res;
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;

	if (is_dot_dotdot(iname)) {
		oname->name[0] = '.';
		oname->name[iname->len - 1] = '.';
		oname->len = iname->len;
		return oname->len;
	}

	if (ci) {
		res = f2fs_fname_encrypt(inode, iname, oname);
		return res;
	}
	/* Without a proper key, a user is not allowed to modify the filenames
	 * in a directory. Consequently, a user space name cannot be mapped to
	 * a disk-space name */
	return -EACCES;
}

int f2fs_fname_setup_filename(struct inode *dir, const struct qstr *iname,
			      int lookup, struct f2fs_filename *fname)
{
	struct f2fs_crypt_info *ci;
	int ret = 0, bigname = 0;

	memset(fname, 0, sizeof(struct f2fs_filename));
	fname->usr_fname = iname;

	if (!f2fs_encrypted_inode(dir) || is_dot_dotdot(iname)) {
		fname->disk_name.name = (unsigned char *)iname->name;
		fname->disk_name.len = iname->len;
		goto out;
	}
	ret = f2fs_setup_fname_crypto(dir);
	if (ret)
		return ret;
	ci = F2FS_I(dir)->i_crypt_info;
	if (ci) {
		ret = f2fs_fname_crypto_alloc_buffer(dir, iname->len,
						     &fname->crypto_buf);
		if (ret < 0)
			goto out;
		ret = f2fs_fname_encrypt(dir, iname, &fname->crypto_buf);
		if (ret < 0)
			goto out;
		fname->disk_name.name = fname->crypto_buf.name;
		fname->disk_name.len = fname->crypto_buf.len;
		ret = 0;
		goto out;
	}
	if (!lookup) {
		ret = -EACCES;
		goto out;
	}

	/* We don't have the key and we are doing a lookup; decode the
	 * user-supplied name
	 */
	if (iname->name[0] == '_')
		bigname = 1;
	if ((bigname && (iname->len != 33)) ||
	    (!bigname && (iname->len > 43))) {
		ret = -ENOENT;
	}
	fname->crypto_buf.name = kmalloc(32, GFP_KERNEL);
	if (fname->crypto_buf.name == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	ret = digest_decode(iname->name + bigname, iname->len - bigname,
				fname->crypto_buf.name);
	if (ret < 0) {
		ret = -ENOENT;
		goto out;
	}
	fname->crypto_buf.len = ret;
	if (bigname) {
		memcpy(&fname->hash, fname->crypto_buf.name, 4);
	} else {
		fname->disk_name.name = fname->crypto_buf.name;
		fname->disk_name.len = fname->crypto_buf.len;
	}
	ret = 0;
out:
	return ret;
}

void f2fs_fname_free_filename(struct f2fs_filename *fname)
{
	kfree(fname->crypto_buf.name);
	fname->crypto_buf.name = NULL;
	fname->usr_fname = NULL;
	fname->disk_name.name = NULL;
}
