/*
 * linux/fs/f2fs/crypto.c
 *
 * Copied from linux/fs/ext4/crypto.c
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * This contains encryption functions for f2fs
 *
 * Written by Michael Halcrow, 2014.
 *
 * Filename encryption additions
 *	Uday Savagaonkar, 2014
 * Encryption policy handling additions
 *	Ildar Muslukhov, 2014
 * Remove ext4_encrypted_zeroout(),
 *   add f2fs_restore_and_release_control_page()
 *	Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 *
 * The usage of AES-XTS should conform to recommendations in NIST
 * Special Publication 800-38E and IEEE P1619/D16.
 */
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <keys/user-type.h>
#include <keys/encrypted-type.h>
#include <linux/crypto.h>
#include <linux/ecryptfs.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/key.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/spinlock_types.h>
#include <linux/f2fs_fs.h>
#include <linux/ratelimit.h>
#include <linux/bio.h>

#include "f2fs.h"
#include "xattr.h"

/* Encryption added and removed here! (L: */

static unsigned int num_prealloc_crypto_pages = 32;
static unsigned int num_prealloc_crypto_ctxs = 128;

module_param(num_prealloc_crypto_pages, uint, 0444);
MODULE_PARM_DESC(num_prealloc_crypto_pages,
		"Number of crypto pages to preallocate");
module_param(num_prealloc_crypto_ctxs, uint, 0444);
MODULE_PARM_DESC(num_prealloc_crypto_ctxs,
		"Number of crypto contexts to preallocate");

static mempool_t *f2fs_bounce_page_pool;

static LIST_HEAD(f2fs_free_crypto_ctxs);
static DEFINE_SPINLOCK(f2fs_crypto_ctx_lock);

struct workqueue_struct *f2fs_read_workqueue;
static DEFINE_MUTEX(crypto_init);

/**
 * f2fs_release_crypto_ctx() - Releases an encryption context
 * @ctx: The encryption context to release.
 *
 * If the encryption context was allocated from the pre-allocated pool, returns
 * it to that pool. Else, frees it.
 *
 * If there's a bounce page in the context, this frees that.
 */
void f2fs_release_crypto_ctx(struct f2fs_crypto_ctx *ctx)
{
	unsigned long flags;

	if (ctx->bounce_page) {
		if (ctx->flags & F2FS_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL)
			__free_page(ctx->bounce_page);
		else
			mempool_free(ctx->bounce_page, f2fs_bounce_page_pool);
		ctx->bounce_page = NULL;
	}
	ctx->control_page = NULL;
	if (ctx->flags & F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL) {
		if (ctx->tfm)
			crypto_free_tfm(ctx->tfm);
		kfree(ctx);
	} else {
		spin_lock_irqsave(&f2fs_crypto_ctx_lock, flags);
		list_add(&ctx->free_list, &f2fs_free_crypto_ctxs);
		spin_unlock_irqrestore(&f2fs_crypto_ctx_lock, flags);
	}
}

/**
 * f2fs_alloc_and_init_crypto_ctx() - Allocates and inits an encryption context
 * @mask: The allocation mask.
 *
 * Return: An allocated and initialized encryption context on success. An error
 * value or NULL otherwise.
 */
static struct f2fs_crypto_ctx *f2fs_alloc_and_init_crypto_ctx(gfp_t mask)
{
	struct f2fs_crypto_ctx *ctx = kzalloc(sizeof(struct f2fs_crypto_ctx),
						mask);

	if (!ctx)
		return ERR_PTR(-ENOMEM);
	return ctx;
}

/**
 * f2fs_get_crypto_ctx() - Gets an encryption context
 * @inode:       The inode for which we are doing the crypto
 *
 * Allocates and initializes an encryption context.
 *
 * Return: An allocated and initialized encryption context on success; error
 * value or NULL otherwise.
 */
struct f2fs_crypto_ctx *f2fs_get_crypto_ctx(struct inode *inode)
{
	struct f2fs_crypto_ctx *ctx = NULL;
	int res = 0;
	unsigned long flags;
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;

	BUG_ON(ci == NULL);
	/*
	 * We first try getting the ctx from a free list because in
	 * the common case the ctx will have an allocated and
	 * initialized crypto tfm, so it's probably a worthwhile
	 * optimization. For the bounce page, we first try getting it
	 * from the kernel allocator because that's just about as fast
	 * as getting it from a list and because a cache of free pages
	 * should generally be a "last resort" option for a filesystem
	 * to be able to do its job.
	 */
	spin_lock_irqsave(&f2fs_crypto_ctx_lock, flags);
	ctx = list_first_entry_or_null(&f2fs_free_crypto_ctxs,
					struct f2fs_crypto_ctx, free_list);
	if (ctx)
		list_del(&ctx->free_list);
	spin_unlock_irqrestore(&f2fs_crypto_ctx_lock, flags);
	if (!ctx) {
		ctx = f2fs_alloc_and_init_crypto_ctx(GFP_NOFS);
		if (IS_ERR(ctx)) {
			res = PTR_ERR(ctx);
			goto out;
		}
		ctx->flags |= F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags &= ~F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	}

	/*
	 * Allocate a new Crypto API context if we don't already have
	 * one or if it isn't the right mode.
	 */
	BUG_ON(ci->ci_mode == F2FS_ENCRYPTION_MODE_INVALID);
	if (ctx->tfm && (ctx->mode != ci->ci_mode)) {
		crypto_free_tfm(ctx->tfm);
		ctx->tfm = NULL;
		ctx->mode = F2FS_ENCRYPTION_MODE_INVALID;
	}
	if (!ctx->tfm) {
		switch (ci->ci_mode) {
		case F2FS_ENCRYPTION_MODE_AES_256_XTS:
			ctx->tfm = crypto_ablkcipher_tfm(
				crypto_alloc_ablkcipher("xts(aes)", 0, 0));
			break;
		case F2FS_ENCRYPTION_MODE_AES_256_GCM:
			/*
			 * TODO(mhalcrow): AEAD w/ gcm(aes);
			 * crypto_aead_setauthsize()
			 */
			ctx->tfm = ERR_PTR(-ENOTSUPP);
			break;
		default:
			BUG();
		}
		if (IS_ERR_OR_NULL(ctx->tfm)) {
			res = PTR_ERR(ctx->tfm);
			ctx->tfm = NULL;
			goto out;
		}
		ctx->mode = ci->ci_mode;
	}
	BUG_ON(ci->ci_size != f2fs_encryption_key_size(ci->ci_mode));

	/*
	 * There shouldn't be a bounce page attached to the crypto
	 * context at this point.
	 */
	BUG_ON(ctx->bounce_page);

out:
	if (res) {
		if (!IS_ERR_OR_NULL(ctx))
			f2fs_release_crypto_ctx(ctx);
		ctx = ERR_PTR(res);
	}
	return ctx;
}

/*
 * Call f2fs_decrypt on every single page, reusing the encryption
 * context.
 */
static void completion_pages(struct work_struct *work)
{
	struct f2fs_crypto_ctx *ctx =
		container_of(work, struct f2fs_crypto_ctx, work);
	struct bio *bio	= ctx->bio;
	struct bio_vec *bv;
	int i;

	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		int ret = f2fs_decrypt(ctx, page);

		if (ret) {
			WARN_ON_ONCE(1);
			SetPageError(page);
		} else
			SetPageUptodate(page);
		unlock_page(page);
	}
	f2fs_release_crypto_ctx(ctx);
	bio_put(bio);
}

void f2fs_end_io_crypto_work(struct f2fs_crypto_ctx *ctx, struct bio *bio)
{
	INIT_WORK(&ctx->work, completion_pages);
	ctx->bio = bio;
	queue_work(f2fs_read_workqueue, &ctx->work);
}

/**
 * f2fs_exit_crypto() - Shutdown the f2fs encryption system
 */
void f2fs_exit_crypto(void)
{
	struct f2fs_crypto_ctx *pos, *n;

	list_for_each_entry_safe(pos, n, &f2fs_free_crypto_ctxs, free_list) {
		if (pos->bounce_page) {
			if (pos->flags &
				F2FS_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL)
				__free_page(pos->bounce_page);
			else
				mempool_free(pos->bounce_page,
						f2fs_bounce_page_pool);
		}
		if (pos->tfm)
			crypto_free_tfm(pos->tfm);
		kfree(pos);
	}
	INIT_LIST_HEAD(&f2fs_free_crypto_ctxs);
	if (f2fs_bounce_page_pool)
		mempool_destroy(f2fs_bounce_page_pool);
	f2fs_bounce_page_pool = NULL;
	if (f2fs_read_workqueue)
		destroy_workqueue(f2fs_read_workqueue);
	f2fs_read_workqueue = NULL;
}

/**
 * f2fs_init_crypto() - Set up for f2fs encryption.
 *
 * We only call this when we start accessing encrypted files, since it
 * results in memory getting allocated that wouldn't otherwise be used.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int f2fs_init_crypto(void)
{
	int i, res;

	mutex_lock(&crypto_init);
	if (f2fs_read_workqueue)
		goto already_initialized;

	f2fs_read_workqueue = alloc_workqueue("f2fs_crypto", WQ_HIGHPRI, 0);
	if (!f2fs_read_workqueue) {
		res = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < num_prealloc_crypto_ctxs; i++) {
		struct f2fs_crypto_ctx *ctx;

		ctx = f2fs_alloc_and_init_crypto_ctx(GFP_KERNEL);
		if (IS_ERR(ctx)) {
			res = PTR_ERR(ctx);
			goto fail;
		}
		list_add(&ctx->free_list, &f2fs_free_crypto_ctxs);
	}

	f2fs_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_crypto_pages, 0);
	if (!f2fs_bounce_page_pool) {
		res = -ENOMEM;
		goto fail;
	}
already_initialized:
	mutex_unlock(&crypto_init);
	return 0;
fail:
	f2fs_exit_crypto();
	mutex_unlock(&crypto_init);
	return res;
}

void f2fs_restore_and_release_control_page(struct page **page)
{
	struct f2fs_crypto_ctx *ctx;
	struct page *bounce_page;

	/* The bounce data pages are unmapped. */
	if ((*page)->mapping)
		return;

	/* The bounce data page is unmapped. */
	bounce_page = *page;
	ctx = (struct f2fs_crypto_ctx *)page_private(bounce_page);

	/* restore control page */
	*page = ctx->control_page;

	f2fs_restore_control_page(bounce_page);
}

void f2fs_restore_control_page(struct page *data_page)
{
	struct f2fs_crypto_ctx *ctx =
		(struct f2fs_crypto_ctx *)page_private(data_page);

	set_page_private(data_page, (unsigned long)NULL);
	ClearPagePrivate(data_page);
	unlock_page(data_page);
	f2fs_release_crypto_ctx(ctx);
}

/**
 * f2fs_crypt_complete() - The completion callback for page encryption
 * @req: The asynchronous encryption request context
 * @res: The result of the encryption operation
 */
static void f2fs_crypt_complete(struct crypto_async_request *req, int res)
{
	struct f2fs_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

typedef enum {
	F2FS_DECRYPT = 0,
	F2FS_ENCRYPT,
} f2fs_direction_t;

static int f2fs_page_crypto(struct f2fs_crypto_ctx *ctx,
				struct inode *inode,
				f2fs_direction_t rw,
				pgoff_t index,
				struct page *src_page,
				struct page *dest_page)
{
	u8 xts_tweak[F2FS_XTS_TWEAK_SIZE];
	struct ablkcipher_request *req = NULL;
	DECLARE_F2FS_COMPLETION_RESULT(ecr);
	struct scatterlist dst, src;
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct crypto_ablkcipher *atfm = __crypto_ablkcipher_cast(ctx->tfm);
	int res = 0;

	BUG_ON(!ctx->tfm);
	BUG_ON(ctx->mode != fi->i_crypt_info->ci_mode);

	if (ctx->mode != F2FS_ENCRYPTION_MODE_AES_256_XTS) {
		printk_ratelimited(KERN_ERR
				"%s: unsupported crypto algorithm: %d\n",
				__func__, ctx->mode);
		return -ENOTSUPP;
	}

	crypto_ablkcipher_clear_flags(atfm, ~0);
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_REQ_WEAK_KEY);

	res = crypto_ablkcipher_setkey(atfm, fi->i_crypt_info->ci_raw,
					fi->i_crypt_info->ci_size);
	if (res) {
		printk_ratelimited(KERN_ERR
				"%s: crypto_ablkcipher_setkey() failed\n",
				__func__);
		return res;
	}
	req = ablkcipher_request_alloc(atfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
				"%s: crypto_request_alloc() failed\n",
				__func__);
		return -ENOMEM;
	}
	ablkcipher_request_set_callback(
		req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		f2fs_crypt_complete, &ecr);

	BUILD_BUG_ON(F2FS_XTS_TWEAK_SIZE < sizeof(index));
	memcpy(xts_tweak, &index, sizeof(index));
	memset(&xts_tweak[sizeof(index)], 0,
			F2FS_XTS_TWEAK_SIZE - sizeof(index));

	sg_init_table(&dst, 1);
	sg_set_page(&dst, dest_page, PAGE_CACHE_SIZE, 0);
	sg_init_table(&src, 1);
	sg_set_page(&src, src_page, PAGE_CACHE_SIZE, 0);
	ablkcipher_request_set_crypt(req, &src, &dst, PAGE_CACHE_SIZE,
					xts_tweak);
	if (rw == F2FS_DECRYPT)
		res = crypto_ablkcipher_decrypt(req);
	else
		res = crypto_ablkcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	ablkcipher_request_free(req);
	if (res) {
		printk_ratelimited(KERN_ERR
			"%s: crypto_ablkcipher_encrypt() returned %d\n",
			__func__, res);
		return res;
	}
	return 0;
}

/**
 * f2fs_encrypt() - Encrypts a page
 * @inode:          The inode for which the encryption should take place
 * @plaintext_page: The page to encrypt. Must be locked.
 *
 * Allocates a ciphertext page and encrypts plaintext_page into it using the ctx
 * encryption context.
 *
 * Called on the page write path.  The caller must call
 * f2fs_restore_control_page() on the returned ciphertext page to
 * release the bounce buffer and the encryption context.
 *
 * Return: An allocated page with the encrypted content on success. Else, an
 * error value or NULL.
 */
struct page *f2fs_encrypt(struct inode *inode,
			  struct page *plaintext_page)
{
	struct f2fs_crypto_ctx *ctx;
	struct page *ciphertext_page = NULL;
	int err;

	BUG_ON(!PageLocked(plaintext_page));

	ctx = f2fs_get_crypto_ctx(inode);
	if (IS_ERR(ctx))
		return (struct page *)ctx;

	/* The encryption operation will require a bounce page. */
	ciphertext_page = alloc_page(GFP_NOFS);
	if (!ciphertext_page) {
		/*
		 * This is a potential bottleneck, but at least we'll have
		 * forward progress.
		 */
		ciphertext_page = mempool_alloc(f2fs_bounce_page_pool,
							GFP_NOFS);
		if (WARN_ON_ONCE(!ciphertext_page))
			ciphertext_page = mempool_alloc(f2fs_bounce_page_pool,
						GFP_NOFS | __GFP_WAIT);
		ctx->flags &= ~F2FS_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags |= F2FS_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->bounce_page = ciphertext_page;
	ctx->control_page = plaintext_page;
	err = f2fs_page_crypto(ctx, inode, F2FS_ENCRYPT, plaintext_page->index,
					plaintext_page, ciphertext_page);
	if (err) {
		f2fs_release_crypto_ctx(ctx);
		return ERR_PTR(err);
	}
	SetPagePrivate(ciphertext_page);
	set_page_private(ciphertext_page, (unsigned long)ctx);
	lock_page(ciphertext_page);
	return ciphertext_page;
}

/**
 * f2fs_decrypt() - Decrypts a page in-place
 * @ctx:  The encryption context.
 * @page: The page to decrypt. Must be locked.
 *
 * Decrypts page in-place using the ctx encryption context.
 *
 * Called from the read completion callback.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int f2fs_decrypt(struct f2fs_crypto_ctx *ctx, struct page *page)
{
	BUG_ON(!PageLocked(page));

	return f2fs_page_crypto(ctx, page->mapping->host,
				F2FS_DECRYPT, page->index, page, page);
}

/*
 * Convenience function which takes care of allocating and
 * deallocating the encryption context
 */
int f2fs_decrypt_one(struct inode *inode, struct page *page)
{
	struct f2fs_crypto_ctx *ctx = f2fs_get_crypto_ctx(inode);
	int ret;

	if (!ctx)
		return -ENOMEM;
	ret = f2fs_decrypt(ctx, page);
	f2fs_release_crypto_ctx(ctx);
	return ret;
}

bool f2fs_valid_contents_enc_mode(uint32_t mode)
{
	return (mode == F2FS_ENCRYPTION_MODE_AES_256_XTS);
}

/**
 * f2fs_validate_encryption_key_size() - Validate the encryption key size
 * @mode: The key mode.
 * @size: The key size to validate.
 *
 * Return: The validated key size for @mode. Zero if invalid.
 */
uint32_t f2fs_validate_encryption_key_size(uint32_t mode, uint32_t size)
{
	if (size == f2fs_encryption_key_size(mode))
		return size;
	return 0;
}
