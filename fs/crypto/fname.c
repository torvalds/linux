/*
 * This contains functions for filename crypto management
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * Written by Uday Savagaonkar, 2014.
 * Modified by Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 */

#include <linux/scatterlist.h>
#include <linux/ratelimit.h>
#include <linux/fscrypto.h>

/**
 * fname_crypt_complete() - completion callback for filename crypto
 * @req: The asynchronous cipher request context
 * @res: The result of the cipher operation
 */
static void fname_crypt_complete(struct crypto_async_request *req, int res)
{
	struct fscrypt_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

/**
 * fname_encrypt() - encrypt a filename
 *
 * The caller must have allocated sufficient memory for the @oname string.
 *
 * Return: 0 on success, -errno on failure
 */
static int fname_encrypt(struct inode *inode,
			const struct qstr *iname, struct fscrypt_str *oname)
{
	struct skcipher_request *req = NULL;
	DECLARE_FS_COMPLETION_RESULT(ecr);
	struct fscrypt_info *ci = inode->i_crypt_info;
	struct crypto_skcipher *tfm = ci->ci_ctfm;
	int res = 0;
	char iv[FS_CRYPTO_BLOCK_SIZE];
	struct scatterlist sg;
	int padding = 4 << (ci->ci_flags & FS_POLICY_FLAGS_PAD_MASK);
	unsigned int lim;
	unsigned int cryptlen;

	lim = inode->i_sb->s_cop->max_namelen(inode);
	if (iname->len <= 0 || iname->len > lim)
		return -EIO;

	/*
	 * Copy the filename to the output buffer for encrypting in-place and
	 * pad it with the needed number of NUL bytes.
	 */
	cryptlen = max_t(unsigned int, iname->len, FS_CRYPTO_BLOCK_SIZE);
	cryptlen = round_up(cryptlen, padding);
	cryptlen = min(cryptlen, lim);
	memcpy(oname->name, iname->name, iname->len);
	memset(oname->name + iname->len, 0, cryptlen - iname->len);

	/* Initialize the IV */
	memset(iv, 0, FS_CRYPTO_BLOCK_SIZE);

	/* Set up the encryption request */
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
			"%s: skcipher_request_alloc() failed\n", __func__);
		return -ENOMEM;
	}
	skcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			fname_crypt_complete, &ecr);
	sg_init_one(&sg, oname->name, cryptlen);
	skcipher_request_set_crypt(req, &sg, &sg, cryptlen, iv);

	/* Do the encryption */
	res = crypto_skcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		/* Request is being completed asynchronously; wait for it */
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	skcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(KERN_ERR
				"%s: Error (error code %d)\n", __func__, res);
		return res;
	}

	oname->len = cryptlen;
	return 0;
}

/**
 * fname_decrypt() - decrypt a filename
 *
 * The caller must have allocated sufficient memory for the @oname string.
 *
 * Return: 0 on success, -errno on failure
 */
static int fname_decrypt(struct inode *inode,
				const struct fscrypt_str *iname,
				struct fscrypt_str *oname)
{
	struct skcipher_request *req = NULL;
	DECLARE_FS_COMPLETION_RESULT(ecr);
	struct scatterlist src_sg, dst_sg;
	struct fscrypt_info *ci = inode->i_crypt_info;
	struct crypto_skcipher *tfm = ci->ci_ctfm;
	int res = 0;
	char iv[FS_CRYPTO_BLOCK_SIZE];
	unsigned lim;

	lim = inode->i_sb->s_cop->max_namelen(inode);
	if (iname->len <= 0 || iname->len > lim)
		return -EIO;

	/* Allocate request */
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
			"%s: crypto_request_alloc() failed\n",  __func__);
		return -ENOMEM;
	}
	skcipher_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		fname_crypt_complete, &ecr);

	/* Initialize IV */
	memset(iv, 0, FS_CRYPTO_BLOCK_SIZE);

	/* Create decryption request */
	sg_init_one(&src_sg, iname->name, iname->len);
	sg_init_one(&dst_sg, oname->name, oname->len);
	skcipher_request_set_crypt(req, &src_sg, &dst_sg, iname->len, iv);
	res = crypto_skcipher_decrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	skcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(KERN_ERR
				"%s: Error (error code %d)\n", __func__, res);
		return res;
	}

	oname->len = strnlen(oname->name, iname->len);
	return 0;
}

static const char *lookup_table =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

/**
 * digest_encode() -
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

u32 fscrypt_fname_encrypted_size(struct inode *inode, u32 ilen)
{
	int padding = 32;
	struct fscrypt_info *ci = inode->i_crypt_info;

	if (ci)
		padding = 4 << (ci->ci_flags & FS_POLICY_FLAGS_PAD_MASK);
	ilen = max(ilen, (u32)FS_CRYPTO_BLOCK_SIZE);
	return round_up(ilen, padding);
}
EXPORT_SYMBOL(fscrypt_fname_encrypted_size);

/**
 * fscrypt_fname_crypto_alloc_obuff() -
 *
 * Allocates an output buffer that is sufficient for the crypto operation
 * specified by the context and the direction.
 */
int fscrypt_fname_alloc_buffer(struct inode *inode,
				u32 ilen, struct fscrypt_str *crypto_str)
{
	unsigned int olen = fscrypt_fname_encrypted_size(inode, ilen);

	crypto_str->len = olen;
	if (olen < FS_FNAME_CRYPTO_DIGEST_SIZE * 2)
		olen = FS_FNAME_CRYPTO_DIGEST_SIZE * 2;
	/*
	 * Allocated buffer can hold one more character to null-terminate the
	 * string
	 */
	crypto_str->name = kmalloc(olen + 1, GFP_NOFS);
	if (!(crypto_str->name))
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(fscrypt_fname_alloc_buffer);

/**
 * fscrypt_fname_crypto_free_buffer() -
 *
 * Frees the buffer allocated for crypto operation.
 */
void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str)
{
	if (!crypto_str)
		return;
	kfree(crypto_str->name);
	crypto_str->name = NULL;
}
EXPORT_SYMBOL(fscrypt_fname_free_buffer);

/**
 * fscrypt_fname_disk_to_usr() - converts a filename from disk space to user
 * space
 *
 * The caller must have allocated sufficient memory for the @oname string.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_fname_disk_to_usr(struct inode *inode,
			u32 hash, u32 minor_hash,
			const struct fscrypt_str *iname,
			struct fscrypt_str *oname)
{
	const struct qstr qname = FSTR_TO_QSTR(iname);
	char buf[24];

	if (fscrypt_is_dot_dotdot(&qname)) {
		oname->name[0] = '.';
		oname->name[iname->len - 1] = '.';
		oname->len = iname->len;
		return 0;
	}

	if (iname->len < FS_CRYPTO_BLOCK_SIZE)
		return -EUCLEAN;

	if (inode->i_crypt_info)
		return fname_decrypt(inode, iname, oname);

	if (iname->len <= FS_FNAME_CRYPTO_DIGEST_SIZE) {
		oname->len = digest_encode(iname->name, iname->len,
					   oname->name);
		return 0;
	}
	if (hash) {
		memcpy(buf, &hash, 4);
		memcpy(buf + 4, &minor_hash, 4);
	} else {
		memset(buf, 0, 8);
	}
	memcpy(buf + 8, iname->name + iname->len - 16, 16);
	oname->name[0] = '_';
	oname->len = 1 + digest_encode(buf, 24, oname->name + 1);
	return 0;
}
EXPORT_SYMBOL(fscrypt_fname_disk_to_usr);

/**
 * fscrypt_fname_usr_to_disk() - converts a filename from user space to disk
 * space
 *
 * The caller must have allocated sufficient memory for the @oname string.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_fname_usr_to_disk(struct inode *inode,
			const struct qstr *iname,
			struct fscrypt_str *oname)
{
	if (fscrypt_is_dot_dotdot(iname)) {
		oname->name[0] = '.';
		oname->name[iname->len - 1] = '.';
		oname->len = iname->len;
		return 0;
	}
	if (inode->i_crypt_info)
		return fname_encrypt(inode, iname, oname);
	/*
	 * Without a proper key, a user is not allowed to modify the filenames
	 * in a directory. Consequently, a user space name cannot be mapped to
	 * a disk-space name
	 */
	return -EACCES;
}
EXPORT_SYMBOL(fscrypt_fname_usr_to_disk);

int fscrypt_setup_filename(struct inode *dir, const struct qstr *iname,
			      int lookup, struct fscrypt_name *fname)
{
	int ret = 0, bigname = 0;

	memset(fname, 0, sizeof(struct fscrypt_name));
	fname->usr_fname = iname;

	if (!dir->i_sb->s_cop->is_encrypted(dir) ||
				fscrypt_is_dot_dotdot(iname)) {
		fname->disk_name.name = (unsigned char *)iname->name;
		fname->disk_name.len = iname->len;
		return 0;
	}
	ret = get_crypt_info(dir);
	if (ret && ret != -EOPNOTSUPP)
		return ret;

	if (dir->i_crypt_info) {
		ret = fscrypt_fname_alloc_buffer(dir, iname->len,
							&fname->crypto_buf);
		if (ret)
			return ret;
		ret = fname_encrypt(dir, iname, &fname->crypto_buf);
		if (ret)
			goto errout;
		fname->disk_name.name = fname->crypto_buf.name;
		fname->disk_name.len = fname->crypto_buf.len;
		return 0;
	}
	if (!lookup)
		return -EACCES;

	/*
	 * We don't have the key and we are doing a lookup; decode the
	 * user-supplied name
	 */
	if (iname->name[0] == '_')
		bigname = 1;
	if ((bigname && (iname->len != 33)) || (!bigname && (iname->len > 43)))
		return -ENOENT;

	fname->crypto_buf.name = kmalloc(32, GFP_KERNEL);
	if (fname->crypto_buf.name == NULL)
		return -ENOMEM;

	ret = digest_decode(iname->name + bigname, iname->len - bigname,
				fname->crypto_buf.name);
	if (ret < 0) {
		ret = -ENOENT;
		goto errout;
	}
	fname->crypto_buf.len = ret;
	if (bigname) {
		memcpy(&fname->hash, fname->crypto_buf.name, 4);
		memcpy(&fname->minor_hash, fname->crypto_buf.name + 4, 4);
	} else {
		fname->disk_name.name = fname->crypto_buf.name;
		fname->disk_name.len = fname->crypto_buf.len;
	}
	return 0;

errout:
	fscrypt_fname_free_buffer(&fname->crypto_buf);
	return ret;
}
EXPORT_SYMBOL(fscrypt_setup_filename);

void fscrypt_free_filename(struct fscrypt_name *fname)
{
	kfree(fname->crypto_buf.name);
	fname->crypto_buf.name = NULL;
	fname->usr_fname = NULL;
	fname->disk_name.name = NULL;
}
EXPORT_SYMBOL(fscrypt_free_filename);
