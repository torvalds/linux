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

static struct workqueue_struct *f2fs_read_workqueue;
static DEFINE_MUTEX(crypto_init);

static struct kmem_cache *f2fs_crypto_ctx_cachep;
struct kmem_cache *f2fs_crypt_info_cachep;

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

	if (ctx->flags & F2FS_WRITE_PATH_FL && ctx->w.bounce_page) {
		mempool_free(ctx->w.bounce_page, f2fs_bounce_page_pool);
		ctx->w.bounce_page = NULL;
	}
	ctx->w.control_page = NULL;
	if (ctx->flags & F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL) {
		kmem_cache_free(f2fs_crypto_ctx_cachep, ctx);
	} else {
		spin_lock_irqsave(&f2fs_crypto_ctx_lock, flags);
		list_add(&ctx->free_list, &f2fs_free_crypto_ctxs);
		spin_unlock_irqrestore(&f2fs_crypto_ctx_lock, flags);
	}
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
	unsigned long flags;
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;

	if (ci == NULL)
		return ERR_PTR(-EACCES);

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
		ctx = kmem_cache_zalloc(f2fs_crypto_ctx_cachep, GFP_NOFS);
		if (!ctx)
			return ERR_PTR(-ENOMEM);
		ctx->flags |= F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags &= ~F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->flags &= ~F2FS_WRITE_PATH_FL;
	return ctx;
}

/*
 * Call f2fs_decrypt on every single page, reusing the encryption
 * context.
 */
static void completion_pages(struct work_struct *work)
{
	struct f2fs_crypto_ctx *ctx =
		container_of(work, struct f2fs_crypto_ctx, r.work);
	struct bio *bio = ctx->r.bio;
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
	INIT_WORK(&ctx->r.work, completion_pages);
	ctx->r.bio = bio;
	queue_work(f2fs_read_workqueue, &ctx->r.work);
}

static void f2fs_crypto_destroy(void)
{
	struct f2fs_crypto_ctx *pos, *n;

	list_for_each_entry_safe(pos, n, &f2fs_free_crypto_ctxs, free_list)
		kmem_cache_free(f2fs_crypto_ctx_cachep, pos);
	INIT_LIST_HEAD(&f2fs_free_crypto_ctxs);
	if (f2fs_bounce_page_pool)
		mempool_destroy(f2fs_bounce_page_pool);
	f2fs_bounce_page_pool = NULL;
}

/**
 * f2fs_crypto_initialize() - Set up for f2fs encryption.
 *
 * We only call this when we start accessing encrypted files, since it
 * results in memory getting allocated that wouldn't otherwise be used.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int f2fs_crypto_initialize(void)
{
	int i, res = -ENOMEM;

	if (f2fs_bounce_page_pool)
		return 0;

	mutex_lock(&crypto_init);
	if (f2fs_bounce_page_pool)
		goto already_initialized;

	for (i = 0; i < num_prealloc_crypto_ctxs; i++) {
		struct f2fs_crypto_ctx *ctx;

		ctx = kmem_cache_zalloc(f2fs_crypto_ctx_cachep, GFP_KERNEL);
		if (!ctx)
			goto fail;
		list_add(&ctx->free_list, &f2fs_free_crypto_ctxs);
	}

	/* must be allocated at the last step to avoid race condition above */
	f2fs_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_crypto_pages, 0);
	if (!f2fs_bounce_page_pool)
		goto fail;

already_initialized:
	mutex_unlock(&crypto_init);
	return 0;
fail:
	f2fs_crypto_destroy();
	mutex_unlock(&crypto_init);
	return res;
}

/**
 * f2fs_exit_crypto() - Shutdown the f2fs encryption system
 */
void f2fs_exit_crypto(void)
{
	f2fs_crypto_destroy();

	if (f2fs_read_workqueue)
		destroy_workqueue(f2fs_read_workqueue);
	if (f2fs_crypto_ctx_cachep)
		kmem_cache_destroy(f2fs_crypto_ctx_cachep);
	if (f2fs_crypt_info_cachep)
		kmem_cache_destroy(f2fs_crypt_info_cachep);
}

int __init f2fs_init_crypto(void)
{
	int res = -ENOMEM;

	f2fs_read_workqueue = alloc_workqueue("f2fs_crypto", WQ_HIGHPRI, 0);
	if (!f2fs_read_workqueue)
		goto fail;

	f2fs_crypto_ctx_cachep = KMEM_CACHE(f2fs_crypto_ctx,
						SLAB_RECLAIM_ACCOUNT);
	if (!f2fs_crypto_ctx_cachep)
		goto fail;

	f2fs_crypt_info_cachep = KMEM_CACHE(f2fs_crypt_info,
						SLAB_RECLAIM_ACCOUNT);
	if (!f2fs_crypt_info_cachep)
		goto fail;

	return 0;
fail:
	f2fs_exit_crypto();
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
	*page = ctx->w.control_page;

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
	struct f2fs_crypt_info *ci = F2FS_I(inode)->i_crypt_info;
	struct crypto_ablkcipher *tfm = ci->ci_ctfm;
	int res = 0;

	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
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
	ciphertext_page = mempool_alloc(f2fs_bounce_page_pool, GFP_NOWAIT);
	if (!ciphertext_page) {
		err = -ENOMEM;
		goto err_out;
	}

	ctx->flags |= F2FS_WRITE_PATH_FL;
	ctx->w.bounce_page = ciphertext_page;
	ctx->w.control_page = plaintext_page;
	err = f2fs_page_crypto(ctx, inode, F2FS_ENCRYPT, plaintext_page->index,
					plaintext_page, ciphertext_page);
	if (err)
		goto err_out;

	SetPagePrivate(ciphertext_page);
	set_page_private(ciphertext_page, (unsigned long)ctx);
	lock_page(ciphertext_page);
	return ciphertext_page;

err_out:
	f2fs_release_crypto_ctx(ctx);
	return ERR_PTR(err);
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
