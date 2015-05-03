/*
 * linux/fs/ext4/crypto_fname.c
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains functions for filename crypto management in ext4
 *
 * Written by Uday Savagaonkar, 2014.
 *
 * This has not yet undergone a rigorous security audit.
 *
 */

#include <crypto/hash.h>
#include <crypto/sha.h>
#include <keys/encrypted-type.h>
#include <keys/user-type.h>
#include <linux/crypto.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/key.h>
#include <linux/key.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/spinlock_types.h>

#include "ext4.h"
#include "ext4_crypto.h"
#include "xattr.h"

/**
 * ext4_dir_crypt_complete() -
 */
static void ext4_dir_crypt_complete(struct crypto_async_request *req, int res)
{
	struct ext4_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

bool ext4_valid_filenames_enc_mode(uint32_t mode)
{
	return (mode == EXT4_ENCRYPTION_MODE_AES_256_CTS);
}

/**
 * ext4_fname_encrypt() -
 *
 * This function encrypts the input filename, and returns the length of the
 * ciphertext. Errors are returned as negative numbers.  We trust the caller to
 * allocate sufficient memory to oname string.
 */
static int ext4_fname_encrypt(struct ext4_fname_crypto_ctx *ctx,
			      const struct qstr *iname,
			      struct ext4_str *oname)
{
	u32 ciphertext_len;
	struct ablkcipher_request *req = NULL;
	DECLARE_EXT4_COMPLETION_RESULT(ecr);
	struct crypto_ablkcipher *tfm = ctx->ctfm;
	int res = 0;
	char iv[EXT4_CRYPTO_BLOCK_SIZE];
	struct scatterlist sg[1];
	char *workbuf;

	if (iname->len <= 0 || iname->len > ctx->lim)
		return -EIO;

	ciphertext_len = (iname->len < EXT4_CRYPTO_BLOCK_SIZE) ?
		EXT4_CRYPTO_BLOCK_SIZE : iname->len;
	ciphertext_len = (ciphertext_len > ctx->lim)
			? ctx->lim : ciphertext_len;

	/* Allocate request */
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(
		    KERN_ERR "%s: crypto_request_alloc() failed\n", __func__);
		return -ENOMEM;
	}
	ablkcipher_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		ext4_dir_crypt_complete, &ecr);

	/* Map the workpage */
	workbuf = kmap(ctx->workpage);

	/* Copy the input */
	memcpy(workbuf, iname->name, iname->len);
	if (iname->len < ciphertext_len)
		memset(workbuf + iname->len, 0, ciphertext_len - iname->len);

	/* Initialize IV */
	memset(iv, 0, EXT4_CRYPTO_BLOCK_SIZE);

	/* Create encryption request */
	sg_init_table(sg, 1);
	sg_set_page(sg, ctx->workpage, PAGE_SIZE, 0);
	ablkcipher_request_set_crypt(req, sg, sg, iname->len, iv);
	res = crypto_ablkcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	if (res >= 0) {
		/* Copy the result to output */
		memcpy(oname->name, workbuf, ciphertext_len);
		res = ciphertext_len;
	}
	kunmap(ctx->workpage);
	ablkcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(
		    KERN_ERR "%s: Error (error code %d)\n", __func__, res);
	}
	oname->len = ciphertext_len;
	return res;
}

/*
 * ext4_fname_decrypt()
 *	This function decrypts the input filename, and returns
 *	the length of the plaintext.
 *	Errors are returned as negative numbers.
 *	We trust the caller to allocate sufficient memory to oname string.
 */
static int ext4_fname_decrypt(struct ext4_fname_crypto_ctx *ctx,
			      const struct ext4_str *iname,
			      struct ext4_str *oname)
{
	struct ext4_str tmp_in[2], tmp_out[1];
	struct ablkcipher_request *req = NULL;
	DECLARE_EXT4_COMPLETION_RESULT(ecr);
	struct scatterlist sg[1];
	struct crypto_ablkcipher *tfm = ctx->ctfm;
	int res = 0;
	char iv[EXT4_CRYPTO_BLOCK_SIZE];
	char *workbuf;

	if (iname->len <= 0 || iname->len > ctx->lim)
		return -EIO;

	tmp_in[0].name = iname->name;
	tmp_in[0].len = iname->len;
	tmp_out[0].name = oname->name;

	/* Allocate request */
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(
		    KERN_ERR "%s: crypto_request_alloc() failed\n",  __func__);
		return -ENOMEM;
	}
	ablkcipher_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		ext4_dir_crypt_complete, &ecr);

	/* Map the workpage */
	workbuf = kmap(ctx->workpage);

	/* Copy the input */
	memcpy(workbuf, iname->name, iname->len);

	/* Initialize IV */
	memset(iv, 0, EXT4_CRYPTO_BLOCK_SIZE);

	/* Create encryption request */
	sg_init_table(sg, 1);
	sg_set_page(sg, ctx->workpage, PAGE_SIZE, 0);
	ablkcipher_request_set_crypt(req, sg, sg, iname->len, iv);
	res = crypto_ablkcipher_decrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	if (res >= 0) {
		/* Copy the result to output */
		memcpy(oname->name, workbuf, iname->len);
		res = iname->len;
	}
	kunmap(ctx->workpage);
	ablkcipher_request_free(req);
	if (res < 0) {
		printk_ratelimited(
		    KERN_ERR "%s: Error in ext4_fname_encrypt (error code %d)\n",
		    __func__, res);
		return res;
	}

	oname->len = strnlen(oname->name, iname->len);
	return oname->len;
}

/**
 * ext4_fname_encode_digest() -
 *
 * Encodes the input digest using characters from the set [a-zA-Z0-9_+].
 * The encoded string is roughly 4/3 times the size of the input string.
 */
int ext4_fname_encode_digest(char *dst, char *src, u32 len)
{
	static const char *lookup_table =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+";
	u32 current_chunk, num_chunks, i;
	char tmp_buf[3];
	u32 c0, c1, c2, c3;

	current_chunk = 0;
	num_chunks = len/3;
	for (i = 0; i < num_chunks; i++) {
		c0 = src[3*i] & 0x3f;
		c1 = (((src[3*i]>>6)&0x3) | ((src[3*i+1] & 0xf)<<2)) & 0x3f;
		c2 = (((src[3*i+1]>>4)&0xf) | ((src[3*i+2] & 0x3)<<4)) & 0x3f;
		c3 = (src[3*i+2]>>2) & 0x3f;
		dst[4*i] = lookup_table[c0];
		dst[4*i+1] = lookup_table[c1];
		dst[4*i+2] = lookup_table[c2];
		dst[4*i+3] = lookup_table[c3];
	}
	if (i*3 < len) {
		memset(tmp_buf, 0, 3);
		memcpy(tmp_buf, &src[3*i], len-3*i);
		c0 = tmp_buf[0] & 0x3f;
		c1 = (((tmp_buf[0]>>6)&0x3) | ((tmp_buf[1] & 0xf)<<2)) & 0x3f;
		c2 = (((tmp_buf[1]>>4)&0xf) | ((tmp_buf[2] & 0x3)<<4)) & 0x3f;
		c3 = (tmp_buf[2]>>2) & 0x3f;
		dst[4*i] = lookup_table[c0];
		dst[4*i+1] = lookup_table[c1];
		dst[4*i+2] = lookup_table[c2];
		dst[4*i+3] = lookup_table[c3];
		i++;
	}
	return (i * 4);
}

/**
 * ext4_fname_hash() -
 *
 * This function computes the hash of the input filename, and sets the output
 * buffer to the *encoded* digest.  It returns the length of the digest as its
 * return value.  Errors are returned as negative numbers.  We trust the caller
 * to allocate sufficient memory to oname string.
 */
static int ext4_fname_hash(struct ext4_fname_crypto_ctx *ctx,
			   const struct ext4_str *iname,
			   struct ext4_str *oname)
{
	struct scatterlist sg;
	struct hash_desc desc = {
		.tfm = (struct crypto_hash *)ctx->htfm,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int res = 0;

	if (iname->len <= EXT4_FNAME_CRYPTO_DIGEST_SIZE) {
		res = ext4_fname_encode_digest(oname->name, iname->name,
					       iname->len);
		oname->len = res;
		return res;
	}

	sg_init_one(&sg, iname->name, iname->len);
	res = crypto_hash_init(&desc);
	if (res) {
		printk(KERN_ERR
		       "%s: Error initializing crypto hash; res = [%d]\n",
		       __func__, res);
		goto out;
	}
	res = crypto_hash_update(&desc, &sg, iname->len);
	if (res) {
		printk(KERN_ERR
		       "%s: Error updating crypto hash; res = [%d]\n",
		       __func__, res);
		goto out;
	}
	res = crypto_hash_final(&desc,
		&oname->name[EXT4_FNAME_CRYPTO_DIGEST_SIZE]);
	if (res) {
		printk(KERN_ERR
		       "%s: Error finalizing crypto hash; res = [%d]\n",
		       __func__, res);
		goto out;
	}
	/* Encode the digest as a printable string--this will increase the
	 * size of the digest */
	oname->name[0] = 'I';
	res = ext4_fname_encode_digest(oname->name+1,
		&oname->name[EXT4_FNAME_CRYPTO_DIGEST_SIZE],
		EXT4_FNAME_CRYPTO_DIGEST_SIZE) + 1;
	oname->len = res;
out:
	return res;
}

/**
 * ext4_free_fname_crypto_ctx() -
 *
 * Frees up a crypto context.
 */
void ext4_free_fname_crypto_ctx(struct ext4_fname_crypto_ctx *ctx)
{
	if (ctx == NULL || IS_ERR(ctx))
		return;

	if (ctx->ctfm && !IS_ERR(ctx->ctfm))
		crypto_free_ablkcipher(ctx->ctfm);
	if (ctx->htfm && !IS_ERR(ctx->htfm))
		crypto_free_hash(ctx->htfm);
	if (ctx->workpage && !IS_ERR(ctx->workpage))
		__free_page(ctx->workpage);
	kfree(ctx);
}

/**
 * ext4_put_fname_crypto_ctx() -
 *
 * Return: The crypto context onto free list. If the free list is above a
 * threshold, completely frees up the context, and returns the memory.
 *
 * TODO: Currently we directly free the crypto context. Eventually we should
 * add code it to return to free list. Such an approach will increase
 * efficiency of directory lookup.
 */
void ext4_put_fname_crypto_ctx(struct ext4_fname_crypto_ctx **ctx)
{
	if (*ctx == NULL || IS_ERR(*ctx))
		return;
	ext4_free_fname_crypto_ctx(*ctx);
	*ctx = NULL;
}

/**
 * ext4_search_fname_crypto_ctx() -
 */
static struct ext4_fname_crypto_ctx *ext4_search_fname_crypto_ctx(
		const struct ext4_encryption_key *key)
{
	return NULL;
}

/**
 * ext4_alloc_fname_crypto_ctx() -
 */
struct ext4_fname_crypto_ctx *ext4_alloc_fname_crypto_ctx(
	const struct ext4_encryption_key *key)
{
	struct ext4_fname_crypto_ctx *ctx;

	ctx = kmalloc(sizeof(struct ext4_fname_crypto_ctx), GFP_NOFS);
	if (ctx == NULL)
		return ERR_PTR(-ENOMEM);
	if (key->mode == EXT4_ENCRYPTION_MODE_INVALID) {
		/* This will automatically set key mode to invalid
		 * As enum for ENCRYPTION_MODE_INVALID is zero */
		memset(&ctx->key, 0, sizeof(ctx->key));
	} else {
		memcpy(&ctx->key, key, sizeof(struct ext4_encryption_key));
	}
	ctx->has_valid_key = (EXT4_ENCRYPTION_MODE_INVALID == key->mode)
		? 0 : 1;
	ctx->ctfm_key_is_ready = 0;
	ctx->ctfm = NULL;
	ctx->htfm = NULL;
	ctx->workpage = NULL;
	return ctx;
}

/**
 * ext4_get_fname_crypto_ctx() -
 *
 * Allocates a free crypto context and initializes it to hold
 * the crypto material for the inode.
 *
 * Return: NULL if not encrypted. Error value on error. Valid pointer otherwise.
 */
struct ext4_fname_crypto_ctx *ext4_get_fname_crypto_ctx(
	struct inode *inode, u32 max_ciphertext_len)
{
	struct ext4_fname_crypto_ctx *ctx;
	struct ext4_inode_info *ei = EXT4_I(inode);
	int res;

	/* Check if the crypto policy is set on the inode */
	res = ext4_encrypted_inode(inode);
	if (res == 0)
		return NULL;

	if (!ext4_has_encryption_key(inode))
		ext4_generate_encryption_key(inode);

	/* Get a crypto context based on the key.
	 * A new context is allocated if no context matches the requested key.
	 */
	ctx = ext4_search_fname_crypto_ctx(&(ei->i_encryption_key));
	if (ctx == NULL)
		ctx = ext4_alloc_fname_crypto_ctx(&(ei->i_encryption_key));
	if (IS_ERR(ctx))
		return ctx;

	if (ctx->has_valid_key) {
		if (ctx->key.mode != EXT4_ENCRYPTION_MODE_AES_256_CTS) {
			printk_once(KERN_WARNING
				    "ext4: unsupported key mode %d\n",
				    ctx->key.mode);
			return ERR_PTR(-ENOKEY);
		}

		/* As a first cut, we will allocate new tfm in every call.
		 * later, we will keep the tfm around, in case the key gets
		 * re-used */
		if (ctx->ctfm == NULL) {
			ctx->ctfm = crypto_alloc_ablkcipher("cts(cbc(aes))",
					0, 0);
		}
		if (IS_ERR(ctx->ctfm)) {
			res = PTR_ERR(ctx->ctfm);
			printk(
			    KERN_DEBUG "%s: error (%d) allocating crypto tfm\n",
			    __func__, res);
			ctx->ctfm = NULL;
			ext4_put_fname_crypto_ctx(&ctx);
			return ERR_PTR(res);
		}
		if (ctx->ctfm == NULL) {
			printk(
			    KERN_DEBUG "%s: could not allocate crypto tfm\n",
			    __func__);
			ext4_put_fname_crypto_ctx(&ctx);
			return ERR_PTR(-ENOMEM);
		}
		if (ctx->workpage == NULL)
			ctx->workpage = alloc_page(GFP_NOFS);
		if (IS_ERR(ctx->workpage)) {
			res = PTR_ERR(ctx->workpage);
			printk(
			    KERN_DEBUG "%s: error (%d) allocating work page\n",
			    __func__, res);
			ctx->workpage = NULL;
			ext4_put_fname_crypto_ctx(&ctx);
			return ERR_PTR(res);
		}
		if (ctx->workpage == NULL) {
			printk(
			    KERN_DEBUG "%s: could not allocate work page\n",
			    __func__);
			ext4_put_fname_crypto_ctx(&ctx);
			return ERR_PTR(-ENOMEM);
		}
		ctx->lim = max_ciphertext_len;
		crypto_ablkcipher_clear_flags(ctx->ctfm, ~0);
		crypto_tfm_set_flags(crypto_ablkcipher_tfm(ctx->ctfm),
			CRYPTO_TFM_REQ_WEAK_KEY);

		/* If we are lucky, we will get a context that is already
		 * set up with the right key. Else, we will have to
		 * set the key */
		if (!ctx->ctfm_key_is_ready) {
			/* Since our crypto objectives for filename encryption
			 * are pretty weak,
			 * we directly use the inode master key */
			res = crypto_ablkcipher_setkey(ctx->ctfm,
					ctx->key.raw, ctx->key.size);
			if (res) {
				ext4_put_fname_crypto_ctx(&ctx);
				return ERR_PTR(-EIO);
			}
			ctx->ctfm_key_is_ready = 1;
		} else {
			/* In the current implementation, key should never be
			 * marked "ready" for a context that has just been
			 * allocated. So we should never reach here */
			 BUG();
		}
	}
	if (ctx->htfm == NULL)
		ctx->htfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(ctx->htfm)) {
		res = PTR_ERR(ctx->htfm);
		printk(KERN_DEBUG "%s: error (%d) allocating hash tfm\n",
			__func__, res);
		ctx->htfm = NULL;
		ext4_put_fname_crypto_ctx(&ctx);
		return ERR_PTR(res);
	}
	if (ctx->htfm == NULL) {
		printk(KERN_DEBUG "%s: could not allocate hash tfm\n",
				__func__);
		ext4_put_fname_crypto_ctx(&ctx);
		return ERR_PTR(-ENOMEM);
	}

	return ctx;
}

/**
 * ext4_fname_crypto_round_up() -
 *
 * Return: The next multiple of block size
 */
u32 ext4_fname_crypto_round_up(u32 size, u32 blksize)
{
	return ((size+blksize-1)/blksize)*blksize;
}

/**
 * ext4_fname_crypto_namelen_on_disk() -
 */
int ext4_fname_crypto_namelen_on_disk(struct ext4_fname_crypto_ctx *ctx,
				      u32 namelen)
{
	u32 ciphertext_len;

	if (ctx == NULL)
		return -EIO;
	if (!(ctx->has_valid_key))
		return -EACCES;
	ciphertext_len = (namelen < EXT4_CRYPTO_BLOCK_SIZE) ?
		EXT4_CRYPTO_BLOCK_SIZE : namelen;
	ciphertext_len = (ciphertext_len > ctx->lim)
			? ctx->lim : ciphertext_len;
	return (int) ciphertext_len;
}

/**
 * ext4_fname_crypto_alloc_obuff() -
 *
 * Allocates an output buffer that is sufficient for the crypto operation
 * specified by the context and the direction.
 */
int ext4_fname_crypto_alloc_buffer(struct ext4_fname_crypto_ctx *ctx,
				   u32 ilen, struct ext4_str *crypto_str)
{
	unsigned int olen;

	if (!ctx)
		return -EIO;
	olen = ext4_fname_crypto_round_up(ilen, EXT4_CRYPTO_BLOCK_SIZE);
	crypto_str->len = olen;
	if (olen < EXT4_FNAME_CRYPTO_DIGEST_SIZE*2)
		olen = EXT4_FNAME_CRYPTO_DIGEST_SIZE*2;
	/* Allocated buffer can hold one more character to null-terminate the
	 * string */
	crypto_str->name = kmalloc(olen+1, GFP_NOFS);
	if (!(crypto_str->name))
		return -ENOMEM;
	return 0;
}

/**
 * ext4_fname_crypto_free_buffer() -
 *
 * Frees the buffer allocated for crypto operation.
 */
void ext4_fname_crypto_free_buffer(struct ext4_str *crypto_str)
{
	if (!crypto_str)
		return;
	kfree(crypto_str->name);
	crypto_str->name = NULL;
}

/**
 * ext4_fname_disk_to_usr() - converts a filename from disk space to user space
 */
int _ext4_fname_disk_to_usr(struct ext4_fname_crypto_ctx *ctx,
			   const struct ext4_str *iname,
			   struct ext4_str *oname)
{
	if (ctx == NULL)
		return -EIO;
	if (iname->len < 3) {
		/*Check for . and .. */
		if (iname->name[0] == '.' && iname->name[iname->len-1] == '.') {
			oname->name[0] = '.';
			oname->name[iname->len-1] = '.';
			oname->len = iname->len;
			return oname->len;
		}
	}
	if (ctx->has_valid_key)
		return ext4_fname_decrypt(ctx, iname, oname);
	else
		return ext4_fname_hash(ctx, iname, oname);
}

int ext4_fname_disk_to_usr(struct ext4_fname_crypto_ctx *ctx,
			   const struct ext4_dir_entry_2 *de,
			   struct ext4_str *oname)
{
	struct ext4_str iname = {.name = (unsigned char *) de->name,
				 .len = de->name_len };

	return _ext4_fname_disk_to_usr(ctx, &iname, oname);
}


/**
 * ext4_fname_usr_to_disk() - converts a filename from user space to disk space
 */
int ext4_fname_usr_to_disk(struct ext4_fname_crypto_ctx *ctx,
			   const struct qstr *iname,
			   struct ext4_str *oname)
{
	int res;

	if (ctx == NULL)
		return -EIO;
	if (iname->len < 3) {
		/*Check for . and .. */
		if (iname->name[0] == '.' &&
				iname->name[iname->len-1] == '.') {
			oname->name[0] = '.';
			oname->name[iname->len-1] = '.';
			oname->len = iname->len;
			return oname->len;
		}
	}
	if (ctx->has_valid_key) {
		res = ext4_fname_encrypt(ctx, iname, oname);
		return res;
	}
	/* Without a proper key, a user is not allowed to modify the filenames
	 * in a directory. Consequently, a user space name cannot be mapped to
	 * a disk-space name */
	return -EACCES;
}

/*
 * Calculate the htree hash from a filename from user space
 */
int ext4_fname_usr_to_hash(struct ext4_fname_crypto_ctx *ctx,
			    const struct qstr *iname,
			    struct dx_hash_info *hinfo)
{
	struct ext4_str tmp, tmp2;
	int ret = 0;

	if (!ctx || !ctx->has_valid_key ||
	    ((iname->name[0] == '.') &&
	     ((iname->len == 1) ||
	      ((iname->name[1] == '.') && (iname->len == 2))))) {
		ext4fs_dirhash(iname->name, iname->len, hinfo);
		return 0;
	}

	/* First encrypt the plaintext name */
	ret = ext4_fname_crypto_alloc_buffer(ctx, iname->len, &tmp);
	if (ret < 0)
		return ret;

	ret = ext4_fname_encrypt(ctx, iname, &tmp);
	if (ret < 0)
		goto out;

	tmp2.len = (4 * ((EXT4_FNAME_CRYPTO_DIGEST_SIZE + 2) / 3)) + 1;
	tmp2.name = kmalloc(tmp2.len + 1, GFP_KERNEL);
	if (tmp2.name == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = ext4_fname_hash(ctx, &tmp, &tmp2);
	if (ret > 0)
		ext4fs_dirhash(tmp2.name, tmp2.len, hinfo);
	ext4_fname_crypto_free_buffer(&tmp2);
out:
	ext4_fname_crypto_free_buffer(&tmp);
	return ret;
}

/**
 * ext4_fname_disk_to_htree() - converts a filename from disk space to htree-access string
 */
int ext4_fname_disk_to_hash(struct ext4_fname_crypto_ctx *ctx,
			    const struct ext4_dir_entry_2 *de,
			    struct dx_hash_info *hinfo)
{
	struct ext4_str iname = {.name = (unsigned char *) de->name,
				 .len = de->name_len};
	struct ext4_str tmp;
	int ret;

	if (!ctx ||
	    ((iname.name[0] == '.') &&
	     ((iname.len == 1) ||
	      ((iname.name[1] == '.') && (iname.len == 2))))) {
		ext4fs_dirhash(iname.name, iname.len, hinfo);
		return 0;
	}

	tmp.len = (4 * ((EXT4_FNAME_CRYPTO_DIGEST_SIZE + 2) / 3)) + 1;
	tmp.name = kmalloc(tmp.len + 1, GFP_KERNEL);
	if (tmp.name == NULL)
		return -ENOMEM;

	ret = ext4_fname_hash(ctx, &iname, &tmp);
	if (ret > 0)
		ext4fs_dirhash(tmp.name, tmp.len, hinfo);
	ext4_fname_crypto_free_buffer(&tmp);
	return ret;
}
