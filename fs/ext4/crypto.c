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

#include <crypto/skcipher.h>
#include <keys/user-type.h>
#include <keys/encrypted-type.h>
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

	if (ctx->flags & EXT4_WRITE_PATH_FL && ctx->w.bounce_page)
		mempool_free(ctx->w.bounce_page, ext4_bounce_page_pool);
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

static int ext4_page_crypto(struct inode *inode,
			    ext4_direction_t rw,
			    pgoff_t index,
			    struct page *src_page,
			    struct page *dest_page)

{
	u8 xts_tweak[EXT4_XTS_TWEAK_SIZE];
	struct skcipher_request *req = NULL;
	DECLARE_EXT4_COMPLETION_RESULT(ecr);
	struct scatterlist dst, src;
	struct ext4_crypt_info *ci = EXT4_I(inode)->i_crypt_info;
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
		ext4_crypt_complete, &ecr);

	BUILD_BUG_ON(EXT4_XTS_TWEAK_SIZE < sizeof(index));
	memcpy(xts_tweak, &index, sizeof(index));
	memset(&xts_tweak[sizeof(index)], 0,
	       EXT4_XTS_TWEAK_SIZE - sizeof(index));

	sg_init_table(&dst, 1);
	sg_set_page(&dst, dest_page, PAGE_CACHE_SIZE, 0);
	sg_init_table(&src, 1);
	sg_set_page(&src, src_page, PAGE_CACHE_SIZE, 0);
	skcipher_request_set_crypt(req, &src, &dst, PAGE_CACHE_SIZE,
				   xts_tweak);
	if (rw == EXT4_DECRYPT)
		res = crypto_skcipher_decrypt(req);
	else
		res = crypto_skcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
	skcipher_request_free(req);
	if (res) {
		printk_ratelimited(
			KERN_ERR
			"%s: crypto_skcipher_encrypt() returned %d\n",
			__func__, res);
		return res;
	}
	return 0;
}

static struct page *alloc_bounce_page(struct ext4_crypto_ctx *ctx)
{
	ctx->w.bounce_page = mempool_alloc(ext4_bounce_page_pool, GFP_NOWAIT);
	if (ctx->w.bounce_page == NULL)
		return ERR_PTR(-ENOMEM);
	ctx->flags |= EXT4_WRITE_PATH_FL;
	return ctx->w.bounce_page;
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
	ciphertext_page = alloc_bounce_page(ctx);
	if (IS_ERR(ciphertext_page))
		goto errout;
	ctx->w.control_page = plaintext_page;
	err = ext4_page_crypto(inode, EXT4_ENCRYPT, plaintext_page->index,
			       plaintext_page, ciphertext_page);
	if (err) {
		ciphertext_page = ERR_PTR(err);
	errout:
		ext4_release_crypto_ctx(ctx);
		return ciphertext_page;
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
int ext4_decrypt(struct page *page)
{
	BUG_ON(!PageLocked(page));

	return ext4_page_crypto(page->mapping->host,
				EXT4_DECRYPT, page->index, page, page);
}

int ext4_encrypted_zeroout(struct inode *inode, ext4_lblk_t lblk,
			   ext4_fsblk_t pblk, ext4_lblk_t len)
{
	struct ext4_crypto_ctx	*ctx;
	struct page		*ciphertext_page = NULL;
	struct bio		*bio;
	int			ret, err = 0;

#if 0
	ext4_msg(inode->i_sb, KERN_CRIT,
		 "ext4_encrypted_zeroout ino %lu lblk %u len %u",
		 (unsigned long) inode->i_ino, lblk, len);
#endif

	BUG_ON(inode->i_sb->s_blocksize != PAGE_CACHE_SIZE);

	ctx = ext4_get_crypto_ctx(inode);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ciphertext_page = alloc_bounce_page(ctx);
	if (IS_ERR(ciphertext_page)) {
		err = PTR_ERR(ciphertext_page);
		goto errout;
	}

	while (len--) {
		err = ext4_page_crypto(inode, EXT4_ENCRYPT, lblk,
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
			ext4_msg(inode->i_sb, KERN_ERR,
				 "bio_add_page failed: %d", ret);
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
		lblk++; pblk++;
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

/*
 * Validate dentries for encrypted directories to make sure we aren't
 * potentially caching stale data after a key has been added or
 * removed.
 */
static int ext4_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct ext4_crypt_info *ci = EXT4_I(dir)->i_crypt_info;
	int dir_has_key, cached_with_key;

	if (!ext4_encrypted_inode(dir))
		return 0;

	if (ci && ci->ci_keyring_key &&
	    (ci->ci_keyring_key->flags & ((1 << KEY_FLAG_INVALIDATED) |
					  (1 << KEY_FLAG_REVOKED) |
					  (1 << KEY_FLAG_DEAD))))
		ci = NULL;

	/* this should eventually be an flag in d_flags */
	cached_with_key = dentry->d_fsdata != NULL;
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
	    (cached_with_key && !dir_has_key)) {
#if 0				/* Revalidation debug */
		char buf[80];
		char *cp = simple_dname(dentry, buf, sizeof(buf));

		if (IS_ERR(cp))
			cp = (char *) "???";
		pr_err("revalidate: %s %p %d %d %d\n", cp, dentry->d_fsdata,
		       cached_with_key, d_is_negative(dentry),
		       dir_has_key);
#endif
		return 0;
	}
	return 1;
}

const struct dentry_operations ext4_encrypted_d_ops = {
	.d_revalidate = ext4_d_revalidate,
};
