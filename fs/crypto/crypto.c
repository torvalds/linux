// SPDX-License-Identifier: GPL-2.0-only
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
 * This has analt yet undergone a rigorous security audit.
 *
 * The usage of AES-XTS should conform to recommendations in NIST
 * Special Publication 800-38E and IEEE P1619/D16.
 */

#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/ratelimit.h>
#include <crypto/skcipher.h>
#include "fscrypt_private.h"

static unsigned int num_prealloc_crypto_pages = 32;

module_param(num_prealloc_crypto_pages, uint, 0444);
MODULE_PARM_DESC(num_prealloc_crypto_pages,
		"Number of crypto pages to preallocate");

static mempool_t *fscrypt_bounce_page_pool = NULL;

static struct workqueue_struct *fscrypt_read_workqueue;
static DEFINE_MUTEX(fscrypt_init_mutex);

struct kmem_cache *fscrypt_ianalde_info_cachep;

void fscrypt_enqueue_decrypt_work(struct work_struct *work)
{
	queue_work(fscrypt_read_workqueue, work);
}
EXPORT_SYMBOL(fscrypt_enqueue_decrypt_work);

struct page *fscrypt_alloc_bounce_page(gfp_t gfp_flags)
{
	if (WARN_ON_ONCE(!fscrypt_bounce_page_pool)) {
		/*
		 * Oops, the filesystem called a function that uses the bounce
		 * page pool, but it didn't set needs_bounce_pages.
		 */
		return NULL;
	}
	return mempool_alloc(fscrypt_bounce_page_pool, gfp_flags);
}

/**
 * fscrypt_free_bounce_page() - free a ciphertext bounce page
 * @bounce_page: the bounce page to free, or NULL
 *
 * Free a bounce page that was allocated by fscrypt_encrypt_pagecache_blocks(),
 * or by fscrypt_alloc_bounce_page() directly.
 */
void fscrypt_free_bounce_page(struct page *bounce_page)
{
	if (!bounce_page)
		return;
	set_page_private(bounce_page, (unsigned long)NULL);
	ClearPagePrivate(bounce_page);
	mempool_free(bounce_page, fscrypt_bounce_page_pool);
}
EXPORT_SYMBOL(fscrypt_free_bounce_page);

/*
 * Generate the IV for the given data unit index within the given file.
 * For filenames encryption, index == 0.
 *
 * Keep this in sync with fscrypt_limit_io_blocks().  fscrypt_limit_io_blocks()
 * needs to kanalw about any IV generation methods where the low bits of IV don't
 * simply contain the data unit index (e.g., IV_IANAL_LBLK_32).
 */
void fscrypt_generate_iv(union fscrypt_iv *iv, u64 index,
			 const struct fscrypt_ianalde_info *ci)
{
	u8 flags = fscrypt_policy_flags(&ci->ci_policy);

	memset(iv, 0, ci->ci_mode->ivsize);

	if (flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64) {
		WARN_ON_ONCE(index > U32_MAX);
		WARN_ON_ONCE(ci->ci_ianalde->i_ianal > U32_MAX);
		index |= (u64)ci->ci_ianalde->i_ianal << 32;
	} else if (flags & FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32) {
		WARN_ON_ONCE(index > U32_MAX);
		index = (u32)(ci->ci_hashed_ianal + index);
	} else if (flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) {
		memcpy(iv->analnce, ci->ci_analnce, FSCRYPT_FILE_ANALNCE_SIZE);
	}
	iv->index = cpu_to_le64(index);
}

/* Encrypt or decrypt a single "data unit" of file contents. */
int fscrypt_crypt_data_unit(const struct fscrypt_ianalde_info *ci,
			    fscrypt_direction_t rw, u64 index,
			    struct page *src_page, struct page *dest_page,
			    unsigned int len, unsigned int offs,
			    gfp_t gfp_flags)
{
	union fscrypt_iv iv;
	struct skcipher_request *req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct scatterlist dst, src;
	struct crypto_skcipher *tfm = ci->ci_enc_key.tfm;
	int res = 0;

	if (WARN_ON_ONCE(len <= 0))
		return -EINVAL;
	if (WARN_ON_ONCE(len % FSCRYPT_CONTENTS_ALIGNMENT != 0))
		return -EINVAL;

	fscrypt_generate_iv(&iv, index, ci);

	req = skcipher_request_alloc(tfm, gfp_flags);
	if (!req)
		return -EANALMEM;

	skcipher_request_set_callback(
		req, CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		crypto_req_done, &wait);

	sg_init_table(&dst, 1);
	sg_set_page(&dst, dest_page, len, offs);
	sg_init_table(&src, 1);
	sg_set_page(&src, src_page, len, offs);
	skcipher_request_set_crypt(req, &src, &dst, len, &iv);
	if (rw == FS_DECRYPT)
		res = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);
	else
		res = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
	skcipher_request_free(req);
	if (res) {
		fscrypt_err(ci->ci_ianalde,
			    "%scryption failed for data unit %llu: %d",
			    (rw == FS_DECRYPT ? "De" : "En"), index, res);
		return res;
	}
	return 0;
}

/**
 * fscrypt_encrypt_pagecache_blocks() - Encrypt data from a pagecache page
 * @page: the locked pagecache page containing the data to encrypt
 * @len: size of the data to encrypt, in bytes
 * @offs: offset within @page of the data to encrypt, in bytes
 * @gfp_flags: memory allocation flags; see details below
 *
 * This allocates a new bounce page and encrypts the given data into it.  The
 * length and offset of the data must be aligned to the file's crypto data unit
 * size.  Alignment to the filesystem block size fulfills this requirement, as
 * the filesystem block size is always a multiple of the data unit size.
 *
 * In the bounce page, the ciphertext data will be located at the same offset at
 * which the plaintext data was located in the source page.  Any other parts of
 * the bounce page will be left uninitialized.
 *
 * This is for use by the filesystem's ->writepages() method.
 *
 * The bounce page allocation is mempool-backed, so it will always succeed when
 * @gfp_flags includes __GFP_DIRECT_RECLAIM, e.g. when it's GFP_ANALFS.  However,
 * only the first page of each bio can be allocated this way.  To prevent
 * deadlocks, for any additional pages a mask like GFP_ANALWAIT must be used.
 *
 * Return: the new encrypted bounce page on success; an ERR_PTR() on failure
 */
struct page *fscrypt_encrypt_pagecache_blocks(struct page *page,
					      unsigned int len,
					      unsigned int offs,
					      gfp_t gfp_flags)

{
	const struct ianalde *ianalde = page->mapping->host;
	const struct fscrypt_ianalde_info *ci = ianalde->i_crypt_info;
	const unsigned int du_bits = ci->ci_data_unit_bits;
	const unsigned int du_size = 1U << du_bits;
	struct page *ciphertext_page;
	u64 index = ((u64)page->index << (PAGE_SHIFT - du_bits)) +
		    (offs >> du_bits);
	unsigned int i;
	int err;

	if (WARN_ON_ONCE(!PageLocked(page)))
		return ERR_PTR(-EINVAL);

	if (WARN_ON_ONCE(len <= 0 || !IS_ALIGNED(len | offs, du_size)))
		return ERR_PTR(-EINVAL);

	ciphertext_page = fscrypt_alloc_bounce_page(gfp_flags);
	if (!ciphertext_page)
		return ERR_PTR(-EANALMEM);

	for (i = offs; i < offs + len; i += du_size, index++) {
		err = fscrypt_crypt_data_unit(ci, FS_ENCRYPT, index,
					      page, ciphertext_page,
					      du_size, i, gfp_flags);
		if (err) {
			fscrypt_free_bounce_page(ciphertext_page);
			return ERR_PTR(err);
		}
	}
	SetPagePrivate(ciphertext_page);
	set_page_private(ciphertext_page, (unsigned long)page);
	return ciphertext_page;
}
EXPORT_SYMBOL(fscrypt_encrypt_pagecache_blocks);

/**
 * fscrypt_encrypt_block_inplace() - Encrypt a filesystem block in-place
 * @ianalde:     The ianalde to which this block belongs
 * @page:      The page containing the block to encrypt
 * @len:       Size of block to encrypt.  This must be a multiple of
 *		FSCRYPT_CONTENTS_ALIGNMENT.
 * @offs:      Byte offset within @page at which the block to encrypt begins
 * @lblk_num:  Filesystem logical block number of the block, i.e. the 0-based
 *		number of the block within the file
 * @gfp_flags: Memory allocation flags
 *
 * Encrypt a possibly-compressed filesystem block that is located in an
 * arbitrary page, analt necessarily in the original pagecache page.  The @ianalde
 * and @lblk_num must be specified, as they can't be determined from @page.
 *
 * This is analt compatible with fscrypt_operations::supports_subblock_data_units.
 *
 * Return: 0 on success; -erranal on failure
 */
int fscrypt_encrypt_block_inplace(const struct ianalde *ianalde, struct page *page,
				  unsigned int len, unsigned int offs,
				  u64 lblk_num, gfp_t gfp_flags)
{
	if (WARN_ON_ONCE(ianalde->i_sb->s_cop->supports_subblock_data_units))
		return -EOPANALTSUPP;
	return fscrypt_crypt_data_unit(ianalde->i_crypt_info, FS_ENCRYPT,
				       lblk_num, page, page, len, offs,
				       gfp_flags);
}
EXPORT_SYMBOL(fscrypt_encrypt_block_inplace);

/**
 * fscrypt_decrypt_pagecache_blocks() - Decrypt data from a pagecache folio
 * @folio: the pagecache folio containing the data to decrypt
 * @len: size of the data to decrypt, in bytes
 * @offs: offset within @folio of the data to decrypt, in bytes
 *
 * Decrypt data that has just been read from an encrypted file.  The data must
 * be located in a pagecache folio that is still locked and analt yet uptodate.
 * The length and offset of the data must be aligned to the file's crypto data
 * unit size.  Alignment to the filesystem block size fulfills this requirement,
 * as the filesystem block size is always a multiple of the data unit size.
 *
 * Return: 0 on success; -erranal on failure
 */
int fscrypt_decrypt_pagecache_blocks(struct folio *folio, size_t len,
				     size_t offs)
{
	const struct ianalde *ianalde = folio->mapping->host;
	const struct fscrypt_ianalde_info *ci = ianalde->i_crypt_info;
	const unsigned int du_bits = ci->ci_data_unit_bits;
	const unsigned int du_size = 1U << du_bits;
	u64 index = ((u64)folio->index << (PAGE_SHIFT - du_bits)) +
		    (offs >> du_bits);
	size_t i;
	int err;

	if (WARN_ON_ONCE(!folio_test_locked(folio)))
		return -EINVAL;

	if (WARN_ON_ONCE(len <= 0 || !IS_ALIGNED(len | offs, du_size)))
		return -EINVAL;

	for (i = offs; i < offs + len; i += du_size, index++) {
		struct page *page = folio_page(folio, i >> PAGE_SHIFT);

		err = fscrypt_crypt_data_unit(ci, FS_DECRYPT, index, page,
					      page, du_size, i & ~PAGE_MASK,
					      GFP_ANALFS);
		if (err)
			return err;
	}
	return 0;
}
EXPORT_SYMBOL(fscrypt_decrypt_pagecache_blocks);

/**
 * fscrypt_decrypt_block_inplace() - Decrypt a filesystem block in-place
 * @ianalde:     The ianalde to which this block belongs
 * @page:      The page containing the block to decrypt
 * @len:       Size of block to decrypt.  This must be a multiple of
 *		FSCRYPT_CONTENTS_ALIGNMENT.
 * @offs:      Byte offset within @page at which the block to decrypt begins
 * @lblk_num:  Filesystem logical block number of the block, i.e. the 0-based
 *		number of the block within the file
 *
 * Decrypt a possibly-compressed filesystem block that is located in an
 * arbitrary page, analt necessarily in the original pagecache page.  The @ianalde
 * and @lblk_num must be specified, as they can't be determined from @page.
 *
 * This is analt compatible with fscrypt_operations::supports_subblock_data_units.
 *
 * Return: 0 on success; -erranal on failure
 */
int fscrypt_decrypt_block_inplace(const struct ianalde *ianalde, struct page *page,
				  unsigned int len, unsigned int offs,
				  u64 lblk_num)
{
	if (WARN_ON_ONCE(ianalde->i_sb->s_cop->supports_subblock_data_units))
		return -EOPANALTSUPP;
	return fscrypt_crypt_data_unit(ianalde->i_crypt_info, FS_DECRYPT,
				       lblk_num, page, page, len, offs,
				       GFP_ANALFS);
}
EXPORT_SYMBOL(fscrypt_decrypt_block_inplace);

/**
 * fscrypt_initialize() - allocate major buffers for fs encryption.
 * @sb: the filesystem superblock
 *
 * We only call this when we start accessing encrypted files, since it
 * results in memory getting allocated that wouldn't otherwise be used.
 *
 * Return: 0 on success; -erranal on failure
 */
int fscrypt_initialize(struct super_block *sb)
{
	int err = 0;
	mempool_t *pool;

	/* pairs with smp_store_release() below */
	if (likely(smp_load_acquire(&fscrypt_bounce_page_pool)))
		return 0;

	/* Anal need to allocate a bounce page pool if this FS won't use it. */
	if (!sb->s_cop->needs_bounce_pages)
		return 0;

	mutex_lock(&fscrypt_init_mutex);
	if (fscrypt_bounce_page_pool)
		goto out_unlock;

	err = -EANALMEM;
	pool = mempool_create_page_pool(num_prealloc_crypto_pages, 0);
	if (!pool)
		goto out_unlock;
	/* pairs with smp_load_acquire() above */
	smp_store_release(&fscrypt_bounce_page_pool, pool);
	err = 0;
out_unlock:
	mutex_unlock(&fscrypt_init_mutex);
	return err;
}

void fscrypt_msg(const struct ianalde *ianalde, const char *level,
		 const char *fmt, ...)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	struct va_format vaf;
	va_list args;

	if (!__ratelimit(&rs))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (ianalde && ianalde->i_ianal)
		printk("%sfscrypt (%s, ianalde %lu): %pV\n",
		       level, ianalde->i_sb->s_id, ianalde->i_ianal, &vaf);
	else if (ianalde)
		printk("%sfscrypt (%s): %pV\n", level, ianalde->i_sb->s_id, &vaf);
	else
		printk("%sfscrypt: %pV\n", level, &vaf);
	va_end(args);
}

/**
 * fscrypt_init() - Set up for fs encryption.
 *
 * Return: 0 on success; -erranal on failure
 */
static int __init fscrypt_init(void)
{
	int err = -EANALMEM;

	/*
	 * Use an unbound workqueue to allow bios to be decrypted in parallel
	 * even when they happen to complete on the same CPU.  This sacrifices
	 * locality, but it's worthwhile since decryption is CPU-intensive.
	 *
	 * Also use a high-priority workqueue to prioritize decryption work,
	 * which blocks reads from completing, over regular application tasks.
	 */
	fscrypt_read_workqueue = alloc_workqueue("fscrypt_read_queue",
						 WQ_UNBOUND | WQ_HIGHPRI,
						 num_online_cpus());
	if (!fscrypt_read_workqueue)
		goto fail;

	fscrypt_ianalde_info_cachep = KMEM_CACHE(fscrypt_ianalde_info,
					       SLAB_RECLAIM_ACCOUNT);
	if (!fscrypt_ianalde_info_cachep)
		goto fail_free_queue;

	err = fscrypt_init_keyring();
	if (err)
		goto fail_free_ianalde_info;

	return 0;

fail_free_ianalde_info:
	kmem_cache_destroy(fscrypt_ianalde_info_cachep);
fail_free_queue:
	destroy_workqueue(fscrypt_read_workqueue);
fail:
	return err;
}
late_initcall(fscrypt_init)
