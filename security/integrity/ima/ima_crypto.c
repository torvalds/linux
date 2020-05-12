// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * File: ima_crypto.c
 *	Calculates md5/sha1 file hash, template hash, boot-aggreate hash
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/ratelimit.h>
#include <linux/file.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <crypto/hash.h>

#include "ima.h"

/* minimum file size for ahash use */
static unsigned long ima_ahash_minsize;
module_param_named(ahash_minsize, ima_ahash_minsize, ulong, 0644);
MODULE_PARM_DESC(ahash_minsize, "Minimum file size for ahash use");

/* default is 0 - 1 page. */
static int ima_maxorder;
static unsigned int ima_bufsize = PAGE_SIZE;

static int param_set_bufsize(const char *val, const struct kernel_param *kp)
{
	unsigned long long size;
	int order;

	size = memparse(val, NULL);
	order = get_order(size);
	if (order >= MAX_ORDER)
		return -EINVAL;
	ima_maxorder = order;
	ima_bufsize = PAGE_SIZE << order;
	return 0;
}

static const struct kernel_param_ops param_ops_bufsize = {
	.set = param_set_bufsize,
	.get = param_get_uint,
};
#define param_check_bufsize(name, p) __param_check(name, p, unsigned int)

module_param_named(ahash_bufsize, ima_bufsize, bufsize, 0644);
MODULE_PARM_DESC(ahash_bufsize, "Maximum ahash buffer size");

static struct crypto_shash *ima_shash_tfm;
static struct crypto_ahash *ima_ahash_tfm;

int __init ima_init_crypto(void)
{
	long rc;

	ima_shash_tfm = crypto_alloc_shash(hash_algo_name[ima_hash_algo], 0, 0);
	if (IS_ERR(ima_shash_tfm)) {
		rc = PTR_ERR(ima_shash_tfm);
		pr_err("Can not allocate %s (reason: %ld)\n",
		       hash_algo_name[ima_hash_algo], rc);
		return rc;
	}
	pr_info("Allocated hash algorithm: %s\n",
		hash_algo_name[ima_hash_algo]);
	return 0;
}

static struct crypto_shash *ima_alloc_tfm(enum hash_algo algo)
{
	struct crypto_shash *tfm = ima_shash_tfm;
	int rc;

	if (algo < 0 || algo >= HASH_ALGO__LAST)
		algo = ima_hash_algo;

	if (algo != ima_hash_algo) {
		tfm = crypto_alloc_shash(hash_algo_name[algo], 0, 0);
		if (IS_ERR(tfm)) {
			rc = PTR_ERR(tfm);
			pr_err("Can not allocate %s (reason: %d)\n",
			       hash_algo_name[algo], rc);
		}
	}
	return tfm;
}

static void ima_free_tfm(struct crypto_shash *tfm)
{
	if (tfm != ima_shash_tfm)
		crypto_free_shash(tfm);
}

/**
 * ima_alloc_pages() - Allocate contiguous pages.
 * @max_size:       Maximum amount of memory to allocate.
 * @allocated_size: Returned size of actual allocation.
 * @last_warn:      Should the min_size allocation warn or not.
 *
 * Tries to do opportunistic allocation for memory first trying to allocate
 * max_size amount of memory and then splitting that until zero order is
 * reached. Allocation is tried without generating allocation warnings unless
 * last_warn is set. Last_warn set affects only last allocation of zero order.
 *
 * By default, ima_maxorder is 0 and it is equivalent to kmalloc(GFP_KERNEL)
 *
 * Return pointer to allocated memory, or NULL on failure.
 */
static void *ima_alloc_pages(loff_t max_size, size_t *allocated_size,
			     int last_warn)
{
	void *ptr;
	int order = ima_maxorder;
	gfp_t gfp_mask = __GFP_RECLAIM | __GFP_NOWARN | __GFP_NORETRY;

	if (order)
		order = min(get_order(max_size), order);

	for (; order; order--) {
		ptr = (void *)__get_free_pages(gfp_mask, order);
		if (ptr) {
			*allocated_size = PAGE_SIZE << order;
			return ptr;
		}
	}

	/* order is zero - one page */

	gfp_mask = GFP_KERNEL;

	if (!last_warn)
		gfp_mask |= __GFP_NOWARN;

	ptr = (void *)__get_free_pages(gfp_mask, 0);
	if (ptr) {
		*allocated_size = PAGE_SIZE;
		return ptr;
	}

	*allocated_size = 0;
	return NULL;
}

/**
 * ima_free_pages() - Free pages allocated by ima_alloc_pages().
 * @ptr:  Pointer to allocated pages.
 * @size: Size of allocated buffer.
 */
static void ima_free_pages(void *ptr, size_t size)
{
	if (!ptr)
		return;
	free_pages((unsigned long)ptr, get_order(size));
}

static struct crypto_ahash *ima_alloc_atfm(enum hash_algo algo)
{
	struct crypto_ahash *tfm = ima_ahash_tfm;
	int rc;

	if (algo < 0 || algo >= HASH_ALGO__LAST)
		algo = ima_hash_algo;

	if (algo != ima_hash_algo || !tfm) {
		tfm = crypto_alloc_ahash(hash_algo_name[algo], 0, 0);
		if (!IS_ERR(tfm)) {
			if (algo == ima_hash_algo)
				ima_ahash_tfm = tfm;
		} else {
			rc = PTR_ERR(tfm);
			pr_err("Can not allocate %s (reason: %d)\n",
			       hash_algo_name[algo], rc);
		}
	}
	return tfm;
}

static void ima_free_atfm(struct crypto_ahash *tfm)
{
	if (tfm != ima_ahash_tfm)
		crypto_free_ahash(tfm);
}

static inline int ahash_wait(int err, struct crypto_wait *wait)
{

	err = crypto_wait_req(err, wait);

	if (err)
		pr_crit_ratelimited("ahash calculation failed: err: %d\n", err);

	return err;
}

static int ima_calc_file_hash_atfm(struct file *file,
				   struct ima_digest_data *hash,
				   struct crypto_ahash *tfm)
{
	loff_t i_size, offset;
	char *rbuf[2] = { NULL, };
	int rc, rbuf_len, active = 0, ahash_rc = 0;
	struct ahash_request *req;
	struct scatterlist sg[1];
	struct crypto_wait wait;
	size_t rbuf_size[2];

	hash->length = crypto_ahash_digestsize(tfm);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				   CRYPTO_TFM_REQ_MAY_SLEEP,
				   crypto_req_done, &wait);

	rc = ahash_wait(crypto_ahash_init(req), &wait);
	if (rc)
		goto out1;

	i_size = i_size_read(file_inode(file));

	if (i_size == 0)
		goto out2;

	/*
	 * Try to allocate maximum size of memory.
	 * Fail if even a single page cannot be allocated.
	 */
	rbuf[0] = ima_alloc_pages(i_size, &rbuf_size[0], 1);
	if (!rbuf[0]) {
		rc = -ENOMEM;
		goto out1;
	}

	/* Only allocate one buffer if that is enough. */
	if (i_size > rbuf_size[0]) {
		/*
		 * Try to allocate secondary buffer. If that fails fallback to
		 * using single buffering. Use previous memory allocation size
		 * as baseline for possible allocation size.
		 */
		rbuf[1] = ima_alloc_pages(i_size - rbuf_size[0],
					  &rbuf_size[1], 0);
	}

	for (offset = 0; offset < i_size; offset += rbuf_len) {
		if (!rbuf[1] && offset) {
			/* Not using two buffers, and it is not the first
			 * read/request, wait for the completion of the
			 * previous ahash_update() request.
			 */
			rc = ahash_wait(ahash_rc, &wait);
			if (rc)
				goto out3;
		}
		/* read buffer */
		rbuf_len = min_t(loff_t, i_size - offset, rbuf_size[active]);
		rc = integrity_kernel_read(file, offset, rbuf[active],
					   rbuf_len);
		if (rc != rbuf_len) {
			if (rc >= 0)
				rc = -EINVAL;
			/*
			 * Forward current rc, do not overwrite with return value
			 * from ahash_wait()
			 */
			ahash_wait(ahash_rc, &wait);
			goto out3;
		}

		if (rbuf[1] && offset) {
			/* Using two buffers, and it is not the first
			 * read/request, wait for the completion of the
			 * previous ahash_update() request.
			 */
			rc = ahash_wait(ahash_rc, &wait);
			if (rc)
				goto out3;
		}

		sg_init_one(&sg[0], rbuf[active], rbuf_len);
		ahash_request_set_crypt(req, sg, NULL, rbuf_len);

		ahash_rc = crypto_ahash_update(req);

		if (rbuf[1])
			active = !active; /* swap buffers, if we use two */
	}
	/* wait for the last update request to complete */
	rc = ahash_wait(ahash_rc, &wait);
out3:
	ima_free_pages(rbuf[0], rbuf_size[0]);
	ima_free_pages(rbuf[1], rbuf_size[1]);
out2:
	if (!rc) {
		ahash_request_set_crypt(req, NULL, hash->digest, 0);
		rc = ahash_wait(crypto_ahash_final(req), &wait);
	}
out1:
	ahash_request_free(req);
	return rc;
}

static int ima_calc_file_ahash(struct file *file, struct ima_digest_data *hash)
{
	struct crypto_ahash *tfm;
	int rc;

	tfm = ima_alloc_atfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	rc = ima_calc_file_hash_atfm(file, hash, tfm);

	ima_free_atfm(tfm);

	return rc;
}

static int ima_calc_file_hash_tfm(struct file *file,
				  struct ima_digest_data *hash,
				  struct crypto_shash *tfm)
{
	loff_t i_size, offset = 0;
	char *rbuf;
	int rc;
	SHASH_DESC_ON_STACK(shash, tfm);

	shash->tfm = tfm;

	hash->length = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(shash);
	if (rc != 0)
		return rc;

	i_size = i_size_read(file_inode(file));

	if (i_size == 0)
		goto out;

	rbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rbuf)
		return -ENOMEM;

	while (offset < i_size) {
		int rbuf_len;

		rbuf_len = integrity_kernel_read(file, offset, rbuf, PAGE_SIZE);
		if (rbuf_len < 0) {
			rc = rbuf_len;
			break;
		}
		if (rbuf_len == 0) {	/* unexpected EOF */
			rc = -EINVAL;
			break;
		}
		offset += rbuf_len;

		rc = crypto_shash_update(shash, rbuf, rbuf_len);
		if (rc)
			break;
	}
	kfree(rbuf);
out:
	if (!rc)
		rc = crypto_shash_final(shash, hash->digest);
	return rc;
}

static int ima_calc_file_shash(struct file *file, struct ima_digest_data *hash)
{
	struct crypto_shash *tfm;
	int rc;

	tfm = ima_alloc_tfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	rc = ima_calc_file_hash_tfm(file, hash, tfm);

	ima_free_tfm(tfm);

	return rc;
}

/*
 * ima_calc_file_hash - calculate file hash
 *
 * Asynchronous hash (ahash) allows using HW acceleration for calculating
 * a hash. ahash performance varies for different data sizes on different
 * crypto accelerators. shash performance might be better for smaller files.
 * The 'ima.ahash_minsize' module parameter allows specifying the best
 * minimum file size for using ahash on the system.
 *
 * If the ima.ahash_minsize parameter is not specified, this function uses
 * shash for the hash calculation.  If ahash fails, it falls back to using
 * shash.
 */
int ima_calc_file_hash(struct file *file, struct ima_digest_data *hash)
{
	loff_t i_size;
	int rc;
	struct file *f = file;
	bool new_file_instance = false, modified_flags = false;

	/*
	 * For consistency, fail file's opened with the O_DIRECT flag on
	 * filesystems mounted with/without DAX option.
	 */
	if (file->f_flags & O_DIRECT) {
		hash->length = hash_digest_size[ima_hash_algo];
		hash->algo = ima_hash_algo;
		return -EINVAL;
	}

	/* Open a new file instance in O_RDONLY if we cannot read */
	if (!(file->f_mode & FMODE_READ)) {
		int flags = file->f_flags & ~(O_WRONLY | O_APPEND |
				O_TRUNC | O_CREAT | O_NOCTTY | O_EXCL);
		flags |= O_RDONLY;
		f = dentry_open(&file->f_path, flags, file->f_cred);
		if (IS_ERR(f)) {
			/*
			 * Cannot open the file again, lets modify f_flags
			 * of original and continue
			 */
			pr_info_ratelimited("Unable to reopen file for reading.\n");
			f = file;
			f->f_flags |= FMODE_READ;
			modified_flags = true;
		} else {
			new_file_instance = true;
		}
	}

	i_size = i_size_read(file_inode(f));

	if (ima_ahash_minsize && i_size >= ima_ahash_minsize) {
		rc = ima_calc_file_ahash(f, hash);
		if (!rc)
			goto out;
	}

	rc = ima_calc_file_shash(f, hash);
out:
	if (new_file_instance)
		fput(f);
	else if (modified_flags)
		f->f_flags &= ~FMODE_READ;
	return rc;
}

/*
 * Calculate the hash of template data
 */
static int ima_calc_field_array_hash_tfm(struct ima_field_data *field_data,
					 struct ima_template_desc *td,
					 int num_fields,
					 struct ima_digest_data *hash,
					 struct crypto_shash *tfm)
{
	SHASH_DESC_ON_STACK(shash, tfm);
	int rc, i;

	shash->tfm = tfm;

	hash->length = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(shash);
	if (rc != 0)
		return rc;

	for (i = 0; i < num_fields; i++) {
		u8 buffer[IMA_EVENT_NAME_LEN_MAX + 1] = { 0 };
		u8 *data_to_hash = field_data[i].data;
		u32 datalen = field_data[i].len;
		u32 datalen_to_hash =
		    !ima_canonical_fmt ? datalen : cpu_to_le32(datalen);

		if (strcmp(td->name, IMA_TEMPLATE_IMA_NAME) != 0) {
			rc = crypto_shash_update(shash,
						(const u8 *) &datalen_to_hash,
						sizeof(datalen_to_hash));
			if (rc)
				break;
		} else if (strcmp(td->fields[i]->field_id, "n") == 0) {
			memcpy(buffer, data_to_hash, datalen);
			data_to_hash = buffer;
			datalen = IMA_EVENT_NAME_LEN_MAX + 1;
		}
		rc = crypto_shash_update(shash, data_to_hash, datalen);
		if (rc)
			break;
	}

	if (!rc)
		rc = crypto_shash_final(shash, hash->digest);

	return rc;
}

int ima_calc_field_array_hash(struct ima_field_data *field_data,
			      struct ima_template_desc *desc, int num_fields,
			      struct ima_digest_data *hash)
{
	struct crypto_shash *tfm;
	int rc;

	tfm = ima_alloc_tfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	rc = ima_calc_field_array_hash_tfm(field_data, desc, num_fields,
					   hash, tfm);

	ima_free_tfm(tfm);

	return rc;
}

static int calc_buffer_ahash_atfm(const void *buf, loff_t len,
				  struct ima_digest_data *hash,
				  struct crypto_ahash *tfm)
{
	struct ahash_request *req;
	struct scatterlist sg;
	struct crypto_wait wait;
	int rc, ahash_rc = 0;

	hash->length = crypto_ahash_digestsize(tfm);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				   CRYPTO_TFM_REQ_MAY_SLEEP,
				   crypto_req_done, &wait);

	rc = ahash_wait(crypto_ahash_init(req), &wait);
	if (rc)
		goto out;

	sg_init_one(&sg, buf, len);
	ahash_request_set_crypt(req, &sg, NULL, len);

	ahash_rc = crypto_ahash_update(req);

	/* wait for the update request to complete */
	rc = ahash_wait(ahash_rc, &wait);
	if (!rc) {
		ahash_request_set_crypt(req, NULL, hash->digest, 0);
		rc = ahash_wait(crypto_ahash_final(req), &wait);
	}
out:
	ahash_request_free(req);
	return rc;
}

static int calc_buffer_ahash(const void *buf, loff_t len,
			     struct ima_digest_data *hash)
{
	struct crypto_ahash *tfm;
	int rc;

	tfm = ima_alloc_atfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	rc = calc_buffer_ahash_atfm(buf, len, hash, tfm);

	ima_free_atfm(tfm);

	return rc;
}

static int calc_buffer_shash_tfm(const void *buf, loff_t size,
				struct ima_digest_data *hash,
				struct crypto_shash *tfm)
{
	SHASH_DESC_ON_STACK(shash, tfm);
	unsigned int len;
	int rc;

	shash->tfm = tfm;

	hash->length = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(shash);
	if (rc != 0)
		return rc;

	while (size) {
		len = size < PAGE_SIZE ? size : PAGE_SIZE;
		rc = crypto_shash_update(shash, buf, len);
		if (rc)
			break;
		buf += len;
		size -= len;
	}

	if (!rc)
		rc = crypto_shash_final(shash, hash->digest);
	return rc;
}

static int calc_buffer_shash(const void *buf, loff_t len,
			     struct ima_digest_data *hash)
{
	struct crypto_shash *tfm;
	int rc;

	tfm = ima_alloc_tfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	rc = calc_buffer_shash_tfm(buf, len, hash, tfm);

	ima_free_tfm(tfm);
	return rc;
}

int ima_calc_buffer_hash(const void *buf, loff_t len,
			 struct ima_digest_data *hash)
{
	int rc;

	if (ima_ahash_minsize && len >= ima_ahash_minsize) {
		rc = calc_buffer_ahash(buf, len, hash);
		if (!rc)
			return 0;
	}

	return calc_buffer_shash(buf, len, hash);
}

static void __init ima_pcrread(u32 idx, struct tpm_digest *d)
{
	if (!ima_tpm_chip)
		return;

	if (tpm_pcr_read(ima_tpm_chip, idx, d) != 0)
		pr_err("Error Communicating to TPM chip\n");
}

/*
 * Calculate the boot aggregate hash
 */
static int __init ima_calc_boot_aggregate_tfm(char *digest,
					      struct crypto_shash *tfm)
{
	struct tpm_digest d = { .alg_id = TPM_ALG_SHA1, .digest = {0} };
	int rc;
	u32 i;
	SHASH_DESC_ON_STACK(shash, tfm);

	shash->tfm = tfm;

	rc = crypto_shash_init(shash);
	if (rc != 0)
		return rc;

	/* cumulative sha1 over tpm registers 0-7 */
	for (i = TPM_PCR0; i < TPM_PCR8; i++) {
		ima_pcrread(i, &d);
		/* now accumulate with current aggregate */
		rc = crypto_shash_update(shash, d.digest, TPM_DIGEST_SIZE);
	}
	if (!rc)
		crypto_shash_final(shash, digest);
	return rc;
}

int __init ima_calc_boot_aggregate(struct ima_digest_data *hash)
{
	struct crypto_shash *tfm;
	int rc;

	tfm = ima_alloc_tfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	hash->length = crypto_shash_digestsize(tfm);
	rc = ima_calc_boot_aggregate_tfm(hash->digest, tfm);

	ima_free_tfm(tfm);

	return rc;
}
