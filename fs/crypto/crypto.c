/*
 * This contains encryption functions for per-file encryption.
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * Written by Michael Halcrow, 2014.
 *
 * Filename encryption additions
 *	Uday Savagaonkar, 2014
 * Encryption policy handling additions
 *	Ildar Muslukhov, 2014
 * Add fscrypt_pullback_bio_page()
 *	Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 *
 * The usage of AES-XTS should conform to recommendations in NIST
 * Special Publication 800-38E and IEEE P1619/D16.
 */

#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/ratelimit.h>
#include <linux/bio.h>
#include <linux/dcache.h>
#include <linux/fscrypto.h>
#include <linux/ecryptfs.h>

static unsigned int num_prealloc_crypto_pages = 32;
static unsigned int num_prealloc_crypto_ctxs = 128;

module_param(num_prealloc_crypto_pages, uint, 0444);
MODULE_PARM_DESC(num_prealloc_crypto_pages,
		"Number of crypto pages to preallocate");
module_param(num_prealloc_crypto_ctxs, uint, 0444);
MODULE_PARM_DESC(num_prealloc_crypto_ctxs,
		"Number of crypto contexts to preallocate");

static mempool_t *fscrypt_bounce_page_pool = NULL;

static LIST_HEAD(fscrypt_free_ctxs);
static DEFINE_SPINLOCK(fscrypt_ctx_lock);

static struct workqueue_struct *fscrypt_read_workqueue;
static DEFINE_MUTEX(fscrypt_init_mutex);

static struct kmem_cache *fscrypt_ctx_cachep;
struct kmem_cache *fscrypt_info_cachep;

/**
 * fscrypt_release_ctx() - Releases an encryption context
 * @ctx: The encryption context to release.
 *
 * If the encryption context was allocated from the pre-allocated pool, returns
 * it to that pool. Else, frees it.
 *
 * If there's a bounce page in the context, this frees that.
 */
void fscrypt_release_ctx(struct fscrypt_ctx *ctx)
{
	unsigned long flags;

	if (ctx->flags & FS_WRITE_PATH_FL && ctx->w.bounce_page) {
		mempool_free(ctx->w.bounce_page, fscrypt_bounce_page_pool);
		ctx->w.bounce_page = NULL;
	}
	ctx->w.control_page = NULL;
	if (ctx->flags & FS_CTX_REQUIRES_FREE_ENCRYPT_FL) {
		kmem_cache_free(fscrypt_ctx_cachep, ctx);
	} else {
		spin_lock_irqsave(&fscrypt_ctx_lock, flags);
		list_add(&ctx->free_list, &fscrypt_free_ctxs);
		spin_unlock_irqrestore(&fscrypt_ctx_lock, flags);
	}
}
EXPORT_SYMBOL(fscrypt_release_ctx);

/**
 * fscrypt_get_ctx() - Gets an encryption context
 * @inode:       The inode for which we are doing the crypto
 *
 * Allocates and initializes an encryption context.
 *
 * Return: An allocated and initialized encryption context on success; error
 * value or NULL otherwise.
 */
struct fscrypt_ctx *fscrypt_get_ctx(struct inode *inode)
{
	struct fscrypt_ctx *ctx = NULL;
	struct fscrypt_info *ci = inode->i_crypt_info;
	unsigned long flags;

	if (ci == NULL)
		return ERR_PTR(-ENOKEY);

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
	spin_lock_irqsave(&fscrypt_ctx_lock, flags);
	ctx = list_first_entry_or_null(&fscrypt_free_ctxs,
					struct fscrypt_ctx, free_list);
	if (ctx)
		list_del(&ctx->free_list);
	spin_unlock_irqrestore(&fscrypt_ctx_lock, flags);
	if (!ctx) {
		ctx = kmem_cache_zalloc(fscrypt_ctx_cachep, GFP_NOFS);
		if (!ctx)
			return ERR_PTR(-ENOMEM);
		ctx->flags |= FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	} else {
		ctx->flags &= ~FS_CTX_REQUIRES_FREE_ENCRYPT_FL;
	}
	ctx->flags &= ~FS_WRITE_PATH_FL;
	return ctx;
}
EXPORT_SYMBOL(fscrypt_get_ctx);

/**
 * fscrypt_complete() - The completion callback for page encryption
 * @req: The asynchronous encryption request context
 * @res: The result of the encryption operation
 */
static void fscrypt_complete(struct crypto_async_request *req, int res)
{
	struct fscrypt_completion_result *ecr = req->data;

	if (res == -EINPROGRESS)
		return;
	ecr->res = res;
	complete(&ecr->completion);
}

typedef enum {
	FS_DECRYPT = 0,
	FS_ENCRYPT,
} fscrypt_direction_t;

static int do_page_crypto(struct inode *inode,
			fscrypt_direction_t rw, pgoff_t index,
			struct page *src_page, struct page *dest_page)
{
	u8 xts_tweak[FS_XTS_TWEAK_SIZE];
	struct skcipher_request *req = NULL;
	DECLARE_FS_COMPLETION_RESULT(ecr);
	struct scatterlist dst, src;
	struct fscrypt_info *ci = inode->i_crypt_info;
	struct crypto_skcipher *tfm = ci->ci_ctfm;
	int res = 0;

	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		printk_ratelimited(KERN_ERR
				"%s: crypto_request_alloc() failed\n",
				__func__);
		return -ENOMEM;
	}

	skcipher_request_set_callback(
		req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		fscrypt_complete, &ecr);

	BUILD_BUG_ON(FS_XTS_TWEAK_SIZE < sizeof(index));
	memcpy(xts_tweak, &index, sizeof(index));
	memset(&xts_tweak[sizeof(index)], 0,
			FS_XTS_TWEAK_SIZE - sizeof(index));

	sg_init_table(&dst, 1);
	sg_set_page(&dst, dest_page, PAGE_CACHE_SIZE, 0);
	sg_init_table(&src, 1);
	sg_set_page(&src, src_page, PAGE_CACHE_SIZE, 0);
	skcipher_request_set_crypt(req, &src, &dst, PAGE_CACHE_SIZE,
					xts_tweak);
	if (rw == FS_DECRYPT)
		res = crypto_skcipher_decrypt(req);
	else
		res = crypto_skcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	skcipher_request_free(req);
	if (res) {
		printk_ratelimited(KERN_ERR
			"%s: crypto_skcipher_encrypt() returned %d\n",
			__func__, res);
		return res;
	}
	return 0;
}

static struct page *alloc_bounce_page(struct fscrypt_ctx *ctx)
{
	ctx->w.bounce_page = mempool_alloc(fscrypt_bounce_page_pool,
							GFP_NOWAIT);
	if (ctx->w.bounce_page == NULL)
		return ERR_PTR(-ENOMEM);
	ctx->flags |= FS_WRITE_PATH_FL;
	return ctx->w.bounce_page;
}

/**
 * fscypt_encrypt_page() - Encrypts a page
 * @inode:          The inode for which the encryption should take place
 * @plaintext_page: The page to encrypt. Must be locked.
 *
 * Allocates a ciphertext page and encrypts plaintext_page into it using the ctx
 * encryption context.
 *
 * Called on the page write path.  The caller must call
 * fscrypt_restore_control_page() on the returned ciphertext page to
 * release the bounce buffer and the encryption context.
 *
 * Return: An allocated page with the encrypted content on success. Else, an
 * error value or NULL.
 */
struct page *fscrypt_encrypt_page(struct inode *inode,
				struct page *plaintext_page)
{
	struct fscrypt_ctx *ctx;
	struct page *ciphertext_page = NULL;
	int err;

	BUG_ON(!PageLocked(plaintext_page));

	ctx = fscrypt_get_ctx(inode);
	if (IS_ERR(ctx))
		return (struct page *)ctx;

	/* The encryption operation will require a bounce page. */
	ciphertext_page = alloc_bounce_page(ctx);
	if (IS_ERR(ciphertext_page))
		goto errout;

	ctx->w.control_page = plaintext_page;
	err = do_page_crypto(inode, FS_ENCRYPT, plaintext_page->index,
					plaintext_page, ciphertext_page);
	if (err) {
		ciphertext_page = ERR_PTR(err);
		goto errout;
	}
	SetPagePrivate(ciphertext_page);
	set_page_private(ciphertext_page, (unsigned long)ctx);
	lock_page(ciphertext_page);
	return ciphertext_page;

errout:
	fscrypt_release_ctx(ctx);
	return ciphertext_page;
}
EXPORT_SYMBOL(fscrypt_encrypt_page);

/**
 * f2crypt_decrypt_page() - Decrypts a page in-place
 * @page: The page to decrypt. Must be locked.
 *
 * Decrypts page in-place using the ctx encryption context.
 *
 * Called from the read completion callback.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int fscrypt_decrypt_page(struct page *page)
{
	BUG_ON(!PageLocked(page));

	return do_page_crypto(page->mapping->host,
			FS_DECRYPT, page->index, page, page);
}
EXPORT_SYMBOL(fscrypt_decrypt_page);

int fscrypt_zeroout_range(struct inode *inode, pgoff_t lblk,
				sector_t pblk, unsigned int len)
{
	struct fscrypt_ctx *ctx;
	struct page *ciphertext_page = NULL;
	struct bio *bio;
	int ret, err = 0;

	BUG_ON(inode->i_sb->s_blocksize != PAGE_CACHE_SIZE);

	ctx = fscrypt_get_ctx(inode);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ciphertext_page = alloc_bounce_page(ctx);
	if (IS_ERR(ciphertext_page)) {
		err = PTR_ERR(ciphertext_page);
		goto errout;
	}

	while (len--) {
		err = do_page_crypto(inode, FS_ENCRYPT, lblk,
						ZERO_PAGE(0), ciphertext_page);
		if (err)
			goto errout;

		bio = bio_alloc(GFP_KERNEL, 1);
		if (!bio) {
			err = -ENOMEM;
			goto errout;
		}
		bio->bi_bdev = inode->i_sb->s_bdev;
		bio->bi_iter.bi_sector =
			pblk << (inode->i_sb->s_blocksize_bits - 9);
		ret = bio_add_page(bio, ciphertext_page,
					inode->i_sb->s_blocksize, 0);
		if (ret != inode->i_sb->s_blocksize) {
			/* should never happen! */
			WARN_ON(1);
			bio_put(bio);
			err = -EIO;
			goto errout;
		}
		err = submit_bio_wait(WRITE, bio);
		if ((err == 0) && bio->bi_error)
			err = -EIO;
		bio_put(bio);
		if (err)
			goto errout;
		lblk++;
		pblk++;
	}
	err = 0;
errout:
	fscrypt_release_ctx(ctx);
	return err;
}
EXPORT_SYMBOL(fscrypt_zeroout_range);

/*
 * Validate dentries for encrypted directories to make sure we aren't
 * potentially caching stale data after a key has been added or
 * removed.
 */
static int fscrypt_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct fscrypt_info *ci = dir->i_crypt_info;
	int dir_has_key, cached_with_key;

	if (!dir->i_sb->s_cop->is_encrypted(dir))
		return 0;

	if (ci && ci->ci_keyring_key &&
	    (ci->ci_keyring_key->flags & ((1 << KEY_FLAG_INVALIDATED) |
					  (1 << KEY_FLAG_REVOKED) |
					  (1 << KEY_FLAG_DEAD))))
		ci = NULL;

	/* this should eventually be an flag in d_flags */
	spin_lock(&dentry->d_lock);
	cached_with_key = dentry->d_flags & DCACHE_ENCRYPTED_WITH_KEY;
	spin_unlock(&dentry->d_lock);
	dir_has_key = (ci != NULL);

	/*
	 * If the dentry was cached without the key, and it is a
	 * negative dentry, it might be a valid name.  We can't check
	 * if the key has since been made available due to locking
	 * reasons, so we fail the validation so ext4_lookup() can do
	 * this check.
	 *
	 * We also fail the validation if the dentry was created with
	 * the key present, but we no longer have the key, or vice versa.
	 */
	if ((!cached_with_key && d_is_negative(dentry)) ||
			(!cached_with_key && dir_has_key) ||
			(cached_with_key && !dir_has_key))
		return 0;
	return 1;
}

const struct dentry_operations fscrypt_d_ops = {
	.d_revalidate = fscrypt_d_revalidate,
};
EXPORT_SYMBOL(fscrypt_d_ops);

/*
 * Call fscrypt_decrypt_page on every single page, reusing the encryption
 * context.
 */
static void completion_pages(struct work_struct *work)
{
	struct fscrypt_ctx *ctx =
		container_of(work, struct fscrypt_ctx, r.work);
	struct bio *bio = ctx->r.bio;
	struct bio_vec *bv;
	int i;

	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		int ret = fscrypt_decrypt_page(page);

		if (ret) {
			WARN_ON_ONCE(1);
			SetPageError(page);
		} else {
			SetPageUptodate(page);
		}
		unlock_page(page);
	}
	fscrypt_release_ctx(ctx);
	bio_put(bio);
}

void fscrypt_decrypt_bio_pages(struct fscrypt_ctx *ctx, struct bio *bio)
{
	INIT_WORK(&ctx->r.work, completion_pages);
	ctx->r.bio = bio;
	queue_work(fscrypt_read_workqueue, &ctx->r.work);
}
EXPORT_SYMBOL(fscrypt_decrypt_bio_pages);

void fscrypt_pullback_bio_page(struct page **page, bool restore)
{
	struct fscrypt_ctx *ctx;
	struct page *bounce_page;

	/* The bounce data pages are unmapped. */
	if ((*page)->mapping)
		return;

	/* The bounce data page is unmapped. */
	bounce_page = *page;
	ctx = (struct fscrypt_ctx *)page_private(bounce_page);

	/* restore control page */
	*page = ctx->w.control_page;

	if (restore)
		fscrypt_restore_control_page(bounce_page);
}
EXPORT_SYMBOL(fscrypt_pullback_bio_page);

void fscrypt_restore_control_page(struct page *page)
{
	struct fscrypt_ctx *ctx;

	ctx = (struct fscrypt_ctx *)page_private(page);
	set_page_private(page, (unsigned long)NULL);
	ClearPagePrivate(page);
	unlock_page(page);
	fscrypt_release_ctx(ctx);
}
EXPORT_SYMBOL(fscrypt_restore_control_page);

static void fscrypt_destroy(void)
{
	struct fscrypt_ctx *pos, *n;

	list_for_each_entry_safe(pos, n, &fscrypt_free_ctxs, free_list)
		kmem_cache_free(fscrypt_ctx_cachep, pos);
	INIT_LIST_HEAD(&fscrypt_free_ctxs);
	mempool_destroy(fscrypt_bounce_page_pool);
	fscrypt_bounce_page_pool = NULL;
}

/**
 * fscrypt_initialize() - allocate major buffers for fs encryption.
 *
 * We only call this when we start accessing encrypted files, since it
 * results in memory getting allocated that wouldn't otherwise be used.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int fscrypt_initialize(void)
{
	int i, res = -ENOMEM;

	if (fscrypt_bounce_page_pool)
		return 0;

	mutex_lock(&fscrypt_init_mutex);
	if (fscrypt_bounce_page_pool)
		goto already_initialized;

	for (i = 0; i < num_prealloc_crypto_ctxs; i++) {
		struct fscrypt_ctx *ctx;

		ctx = kmem_cache_zalloc(fscrypt_ctx_cachep, GFP_NOFS);
		if (!ctx)
			goto fail;
		list_add(&ctx->free_list, &fscrypt_free_ctxs);
	}

	fscrypt_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_crypto_pages, 0);
	if (!fscrypt_bounce_page_pool)
		goto fail;

already_initialized:
	mutex_unlock(&fscrypt_init_mutex);
	return 0;
fail:
	fscrypt_destroy();
	mutex_unlock(&fscrypt_init_mutex);
	return res;
}
EXPORT_SYMBOL(fscrypt_initialize);

/**
 * fscrypt_init() - Set up for fs encryption.
 */
static int __init fscrypt_init(void)
{
	fscrypt_read_workqueue = alloc_workqueue("fscrypt_read_queue",
							WQ_HIGHPRI, 0);
	if (!fscrypt_read_workqueue)
		goto fail;

	fscrypt_ctx_cachep = KMEM_CACHE(fscrypt_ctx, SLAB_RECLAIM_ACCOUNT);
	if (!fscrypt_ctx_cachep)
		goto fail_free_queue;

	fscrypt_info_cachep = KMEM_CACHE(fscrypt_info, SLAB_RECLAIM_ACCOUNT);
	if (!fscrypt_info_cachep)
		goto fail_free_ctx;

	return 0;

fail_free_ctx:
	kmem_cache_destroy(fscrypt_ctx_cachep);
fail_free_queue:
	destroy_workqueue(fscrypt_read_workqueue);
fail:
	return -ENOMEM;
}
module_init(fscrypt_init)

/**
 * fscrypt_exit() - Shutdown the fs encryption system
 */
static void __exit fscrypt_exit(void)
{
	fscrypt_destroy();

	if (fscrypt_read_workqueue)
		destroy_workqueue(fscrypt_read_workqueue);
	kmem_cache_destroy(fscrypt_ctx_cachep);
	kmem_cache_destroy(fscrypt_info_cachep);
}
module_exit(fscrypt_exit);

MODULE_LICENSE("GPL");
