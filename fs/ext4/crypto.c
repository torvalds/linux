/*
 * linux/fs/ext4/crypto.c
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption functions for ext4
 *
 * Written by Michael Halcrow, 2014.
 *
 * Filename encryption additions
 *	Uday Savagaonkar, 2014
 * Encryption policy handling additions
 *	Ildar Muslukhov, 2014
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

#include "ext4_extents.h"
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

static mempool_t *ext4_bounce_page_pool;

static LIST_HEAD(ext4_free_crypto_ctxs);
static DEFINE_SPINLOCK(ext4_crypto_ctx_lock);

static struct kmem_cache *ext4_crypto_ctx_cachep;
struct kmem_cache *ext4_crypt_info_cachep;

/**
 * ext4_release_crypto_ctx() - Releases an encryption context
 * @ctx: The encryption context to release.
 *
 * If the encryption context was allocated from the pre-allocated pool, returns
 * it to that pool. Else, frees it.
 *
 * If there's a bounce page in the context, this frees that.
 */
void ext4_release_crypto_ctx(struct ext4_crypto_ctx *ctx)
{
	unsigned long flags;

	if (ctx->flags & EXT4_WRITE_PATH_FL && ctx->w.bounce_page) {
		if (ctx->flags & EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL)
			__free_page(ctx->w.bounce_page);
		else
			mempool_free(ctx->w.bounce_page, ext4_bounce_page_pool);
	}
	ctx->w.bounce_page = NULL;
	ctx->w.control_page = NULL;
	if (ctx->flags & EXT4_CTX_REQUIRES_FREE_ENCRYPT_FL) {
		kmem_cache_free(ext4_crypto_ctx_cachep, ctx);
	} else {
		spin_lock_irqsave(&ext4_crypto_ctx_lock, flags);
		list_add(&ctx->free_list, &ext4_free_crypto_ctxs);
		spin_unlock_irqrestore(&ext4_crypto_ctx_lock, flags);
	}
}

/**
 * ext4_get_crypto_ctx() - Gets an encryption context
 * @inode:       The inode for which we are doing the crypto
 *
 * Allocates and initializes an encryption context.
 *
 * Return: An allocated and initialized encryption context on success; error
 * value or NULL otherwise.
 */
struct ext4_crypto_ctx *ext4_get_crypto_ctx(struct inode *inode)
{
	struct ext4_crypto_ctx *ctx = NULL;
	int res = 0;
	unsigned long flags;
	struct ext4_crypt_info *ci = EXT4_I(inode)->i_crypt_info;

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
	spin_lock_irqsave(&ext4_crypto_ctx_lock, flags);
	ctx = list_first_entry_or_null(&ext4_free_crypto_ctxs,
				       struct ext4_crypto_ctx, free_list);
	if (ctx)
		list_del(&ctx->free_list);
	spin_unlock_irqrestore(&ext4_crypto_ctx_lock, flags);
	if (!ctx) {
		ctx = kmem_cache_zalloc(ext4_crypto_ctx_cachep, GFP_NOFS);
		if (!ctx) {
			res = -ENOMEM;
			goto out;
		}
		ctx->flags |= EXT4_CTX_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags &= ~EXT4_CTX_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->flags &= ~EXT4_WRITE_PATH_FL;

out:
	if (res) {
		if (!IS_ERR_OR_NULL(ctx))
			ext4_release_crypto_ctx(ctx);
		ctx = ERR_PTR(res);
	}
	return ctx;
}

struct workqueue_struct *ext4_read_workqueue;
static DEFINE_MUTEX(crypto_init);

/**
 * ext4_exit_crypto() - Shutdown the ext4 encryption system
 */
void ext4_exit_crypto(void)
{
	struct ext4_crypto_ctx *pos, *n;

	list_for_each_entry_safe(pos, n, &ext4_free_crypto_ctxs, free_list)
		kmem_cache_free(ext4_crypto_ctx_cachep, pos);
	INIT_LIST_HEAD(&ext4_free_crypto_ctxs);
	if (ext4_bounce_page_pool)
		mempool_destroy(ext4_bounce_page_pool);
	ext4_bounce_page_pool = NULL;
	if (ext4_read_workqueue)
		destroy_workqueue(ext4_read_workqueue);
	ext4_read_workqueue = NULL;
	if (ext4_crypto_ctx_cachep)
		kmem_cache_destroy(ext4_crypto_ctx_cachep);
	ext4_crypto_ctx_cachep = NULL;
	if (ext4_crypt_info_cachep)
		kmem_cache_destroy(ext4_crypt_info_cachep);
	ext4_crypt_info_cachep = NULL;
}

/**
 * ext4_init_crypto() - Set up for ext4 encryption.
 *
 * We only call this when we start accessing encrypted files, since it
 * results in memory getting allocated that wouldn't otherwise be used.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int ext4_init_crypto(void)
{
	int i, res = -ENOMEM;

	mutex_lock(&crypto_init);
	if (ext4_read_workqueue)
		goto already_initialized;
	ext4_read_workqueue = alloc_workqueue("ext4_crypto", WQ_HIGHPRI, 0);
	if (!ext4_read_workqueue)
		goto fail;

	ext4_crypto_ctx_cachep = KMEM_CACHE(ext4_crypto_ctx,
					    SLAB_RECLAIM_ACCOUNT);
	if (!ext4_crypto_ctx_cachep)
		goto fail;

	ext4_crypt_info_cachep = KMEM_CACHE(ext4_crypt_info,
					    SLAB_RECLAIM_ACCOUNT);
	if (!ext4_crypt_info_cachep)
		goto fail;

	for (i = 0; i < num_prealloc_crypto_ctxs; i++) {
		struct ext4_crypto_ctx *ctx;

		ctx = kmem_cache_zalloc(ext4_crypto_ctx_cachep, GFP_NOFS);
		if (!ctx) {
			res = -ENOMEM;
			goto fail;
		}
		list_add(&ctx->free_list, &ext4_free_crypto_ctxs);
	}

	ext4_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_crypto_pages, 0);
	if (!ext4_bounce_page_pool) {
		res = -ENOMEM;
		goto fail;
	}
already_initialized:
	mutex_unlock(&crypto_init);
	return 0;
fail:
	ext4_exit_crypto();
	mutex_unlock(&crypto_init);
	return res;
}

void ext4_restore_control_page(struct page *data_page)
{
	struct ext4_crypto_ctx *ctx =
		(struct ext4_crypto_ctx *)page_private(data_page);

	set_page_private(data_page, (unsigned long)NULL);
	ClearPagePrivate(data_page);
	unlock_page(data_page);
	ext4_release_crypto_ctx(ctx);
}

/**
 * ext4_crypt_complete() - The completion callback for page encryption
 * @req: The asynchronous encryption request context
 * @res: The result of the encryption operation
 */
static void ext4_crypt_complete(struct crypto_async_request *req, int res)
{
	struct ext4_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

typedef enum {
	EXT4_DECRYPT = 0,
	EXT4_ENCRYPT,
} ext4_direction_t;

static int ext4_page_crypto(struct ext4_crypto_ctx *ctx,
			    struct inode *inode,
			    ext4_direction_t rw,
			    pgoff_t index,
			    struct page *src_page,
			    struct page *dest_page)

{
	u8 xts_tweak[EXT4_XTS_TWEAK_SIZE];
	struct ablkcipher_request *req = NULL;
	DECLARE_EXT4_COMPLETION_RESULT(ecr);
	struct scatterlist dst, src;
	struct ext4_crypt_info *ci = EXT4_I(inode)->i_crypt_info;
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
		ext4_crypt_complete, &ecr);

	BUILD_BUG_ON(EXT4_XTS_TWEAK_SIZE < sizeof(index));
	memcpy(xts_tweak, &index, sizeof(index));
	memset(&xts_tweak[sizeof(index)], 0,
	       EXT4_XTS_TWEAK_SIZE - sizeof(index));

	sg_init_table(&dst, 1);
	sg_set_page(&dst, dest_page, PAGE_CACHE_SIZE, 0);
	sg_init_table(&src, 1);
	sg_set_page(&src, src_page, PAGE_CACHE_SIZE, 0);
	ablkcipher_request_set_crypt(req, &src, &dst, PAGE_CACHE_SIZE,
				     xts_tweak);
	if (rw == EXT4_DECRYPT)
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
		printk_ratelimited(
			KERN_ERR
			"%s: crypto_ablkcipher_encrypt() returned %d\n",
			__func__, res);
		return res;
	}
	return 0;
}

/**
 * ext4_encrypt() - Encrypts a page
 * @inode:          The inode for which the encryption should take place
 * @plaintext_page: The page to encrypt. Must be locked.
 *
 * Allocates a ciphertext page and encrypts plaintext_page into it using the ctx
 * encryption context.
 *
 * Called on the page write path.  The caller must call
 * ext4_restore_control_page() on the returned ciphertext page to
 * release the bounce buffer and the encryption context.
 *
 * Return: An allocated page with the encrypted content on success. Else, an
 * error value or NULL.
 */
struct page *ext4_encrypt(struct inode *inode,
			  struct page *plaintext_page)
{
	struct ext4_crypto_ctx *ctx;
	struct page *ciphertext_page = NULL;
	int err;

	BUG_ON(!PageLocked(plaintext_page));

	ctx = ext4_get_crypto_ctx(inode);
	if (IS_ERR(ctx))
		return (struct page *) ctx;

	/* The encryption operation will require a bounce page. */
	ciphertext_page = alloc_page(GFP_NOFS);
	if (!ciphertext_page) {
		/* This is a potential bottleneck, but at least we'll have
		 * forward progress. */
		ciphertext_page = mempool_alloc(ext4_bounce_page_pool,
						 GFP_NOFS);
		if (WARN_ON_ONCE(!ciphertext_page)) {
			ciphertext_page = mempool_alloc(ext4_bounce_page_pool,
							 GFP_NOFS | __GFP_WAIT);
		}
		ctx->flags &= ~EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags |= EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->flags |= EXT4_WRITE_PATH_FL;
	ctx->w.bounce_page = ciphertext_page;
	ctx->w.control_page = plaintext_page;
	err = ext4_page_crypto(ctx, inode, EXT4_ENCRYPT, plaintext_page->index,
			       plaintext_page, ciphertext_page);
	if (err) {
		ext4_release_crypto_ctx(ctx);
		return ERR_PTR(err);
	}
	SetPagePrivate(ciphertext_page);
	set_page_private(ciphertext_page, (unsigned long)ctx);
	lock_page(ciphertext_page);
	return ciphertext_page;
}

/**
 * ext4_decrypt() - Decrypts a page in-place
 * @ctx:  The encryption context.
 * @page: The page to decrypt. Must be locked.
 *
 * Decrypts page in-place using the ctx encryption context.
 *
 * Called from the read completion callback.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int ext4_decrypt(struct ext4_crypto_ctx *ctx, struct page *page)
{
	BUG_ON(!PageLocked(page));

	return ext4_page_crypto(ctx, page->mapping->host,
				EXT4_DECRYPT, page->index, page, page);
}

/*
 * Convenience function which takes care of allocating and
 * deallocating the encryption context
 */
int ext4_decrypt_one(struct inode *inode, struct page *page)
{
	int ret;

	struct ext4_crypto_ctx *ctx = ext4_get_crypto_ctx(inode);

	if (!ctx)
		return -ENOMEM;
	ret = ext4_decrypt(ctx, page);
	ext4_release_crypto_ctx(ctx);
	return ret;
}

int ext4_encrypted_zeroout(struct inode *inode, struct ext4_extent *ex)
{
	struct ext4_crypto_ctx	*ctx;
	struct page		*ciphertext_page = NULL;
	struct bio		*bio;
	ext4_lblk_t		lblk = ex->ee_block;
	ext4_fsblk_t		pblk = ext4_ext_pblock(ex);
	unsigned int		len = ext4_ext_get_actual_len(ex);
	int			err = 0;

	BUG_ON(inode->i_sb->s_blocksize != PAGE_CACHE_SIZE);

	ctx = ext4_get_crypto_ctx(inode);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ciphertext_page = alloc_page(GFP_NOFS);
	if (!ciphertext_page) {
		/* This is a potential bottleneck, but at least we'll have
		 * forward progress. */
		ciphertext_page = mempool_alloc(ext4_bounce_page_pool,
						 GFP_NOFS);
		if (WARN_ON_ONCE(!ciphertext_page)) {
			ciphertext_page = mempool_alloc(ext4_bounce_page_pool,
							 GFP_NOFS | __GFP_WAIT);
		}
		ctx->flags &= ~EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags |= EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->w.bounce_page = ciphertext_page;

	while (len--) {
		err = ext4_page_crypto(ctx, inode, EXT4_ENCRYPT, lblk,
				       ZERO_PAGE(0), ciphertext_page);
		if (err)
			goto errout;

		bio = bio_alloc(GFP_KERNEL, 1);
		if (!bio) {
			err = -ENOMEM;
			goto errout;
		}
		bio->bi_bdev = inode->i_sb->s_bdev;
		bio->bi_iter.bi_sector = pblk;
		err = bio_add_page(bio, ciphertext_page,
				   inode->i_sb->s_blocksize, 0);
		if (err) {
			bio_put(bio);
			goto errout;
		}
		err = submit_bio_wait(WRITE, bio);
		if (err)
			goto errout;
	}
	err = 0;
errout:
	ext4_release_crypto_ctx(ctx);
	return err;
}

bool ext4_valid_contents_enc_mode(uint32_t mode)
{
	return (mode == EXT4_ENCRYPTION_MODE_AES_256_XTS);
}

/**
 * ext4_validate_encryption_key_size() - Validate the encryption key size
 * @mode: The key mode.
 * @size: The key size to validate.
 *
 * Return: The validated key size for @mode. Zero if invalid.
 */
uint32_t ext4_validate_encryption_key_size(uint32_t mode, uint32_t size)
{
	if (size == ext4_encryption_key_size(mode))
		return size;
	return 0;
}
