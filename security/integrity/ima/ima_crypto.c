/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
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
#include <crypto/hash_info.h>
#include "ima.h"

struct ahash_completion {
	struct completion completion;
	int err;
};

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

static struct kernel_param_ops param_ops_bufsize = {
	.set = param_set_bufsize,
	.get = param_get_uint,
};
#define param_check_bufsize(name, p) __param_check(name, p, unsigned int)

module_param_named(ahash_bufsize, ima_bufsize, bufsize, 0644);
MODULE_PARM_DESC(ahash_bufsize, "Maximum ahash buffer size");

static struct crypto_shash *ima_shash_tfm;
static struct crypto_ahash *ima_ahash_tfm;

/**
 * ima_kernel_read - read file content
 *
 * This is a function for reading file content instead of kernel_read().
 * It does not perform locking checks to ensure it cannot be blocked.
 * It does not perform security checks because it is irrelevant for IMA.
 *
 */
static int ima_kernel_read(struct file *file, loff_t offset,
			   char *addr, unsigned long count)
{
	mm_segment_t old_fs;
	char __user *buf = addr;
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!file->f_op->read && !file->f_op->aio_read)
		return -EINVAL;

	old_fs = get_fs();
	set_fs(get_ds());
	if (file->f_op->read)
		ret = file->f_op->read(file, buf, count, &offset);
	else
		ret = do_sync_read(file, buf, count, &offset);
	set_fs(old_fs);
	return ret;
}

int ima_init_crypto(void)
{
	long rc;

	ima_shash_tfm = crypto_alloc_shash(hash_algo_name[ima_hash_algo], 0, 0);
	if (IS_ERR(ima_shash_tfm)) {
		rc = PTR_ERR(ima_shash_tfm);
		pr_err("Can not allocate %s (reason: %ld)\n",
		       hash_algo_name[ima_hash_algo], rc);
		return rc;
	}
	return 0;
}

static struct crypto_shash *ima_alloc_tfm(enum hash_algo algo)
{
	struct crypto_shash *tfm = ima_shash_tfm;
	int rc;

	if (algo != ima_hash_algo && algo < HASH_ALGO__LAST) {
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
	gfp_t gfp_mask = __GFP_WAIT | __GFP_NOWARN | __GFP_NORETRY;

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

	if ((algo != ima_hash_algo && algo < HASH_ALGO__LAST) || !tfm) {
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

static void ahash_complete(struct crypto_async_request *req, int err)
{
	struct ahash_completion *res = req->data;

	if (err == -EINPROGRESS)
		return;
	res->err = err;
	complete(&res->completion);
}

static int ahash_wait(int err, struct ahash_completion *res)
{
	switch (err) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		wait_for_completion(&res->completion);
		reinit_completion(&res->completion);
		err = res->err;
		/* fall through */
	default:
		pr_crit_ratelimited("ahash calculation failed: err: %d\n", err);
	}

	return err;
}

static int ima_calc_file_hash_atfm(struct file *file,
				   struct ima_digest_data *hash,
				   struct crypto_ahash *tfm)
{
	loff_t i_size, offset;
	char *rbuf;
	int rc, read = 0, rbuf_len;
	struct ahash_request *req;
	struct scatterlist sg[1];
	struct ahash_completion res;
	size_t rbuf_size;

	hash->length = crypto_ahash_digestsize(tfm);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	init_completion(&res.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				   CRYPTO_TFM_REQ_MAY_SLEEP,
				   ahash_complete, &res);

	rc = ahash_wait(crypto_ahash_init(req), &res);
	if (rc)
		goto out1;

	i_size = i_size_read(file_inode(file));

	if (i_size == 0)
		goto out2;

	/*
	 * Try to allocate maximum size of memory.
	 * Fail if even a single page cannot be allocated.
	 */
	rbuf = ima_alloc_pages(i_size, &rbuf_size, 1);
	if (!rbuf) {
		rc = -ENOMEM;
		goto out1;
	}

	if (!(file->f_mode & FMODE_READ)) {
		file->f_mode |= FMODE_READ;
		read = 1;
	}

	for (offset = 0; offset < i_size; offset += rbuf_len) {
		rbuf_len = ima_kernel_read(file, offset, rbuf, PAGE_SIZE);
		if (rbuf_len < 0) {
			rc = rbuf_len;
			break;
		}
		if (rbuf_len == 0)
			break;

		sg_init_one(&sg[0], rbuf, rbuf_len);
		ahash_request_set_crypt(req, sg, NULL, rbuf_len);

		rc = ahash_wait(crypto_ahash_update(req), &res);
		if (rc)
			break;
	}
	if (read)
		file->f_mode &= ~FMODE_READ;
	ima_free_pages(rbuf, rbuf_size);
out2:
	if (!rc) {
		ahash_request_set_crypt(req, NULL, hash->digest, 0);
		rc = ahash_wait(crypto_ahash_final(req), &res);
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
	int rc, read = 0;
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(tfm)];
	} desc;

	desc.shash.tfm = tfm;
	desc.shash.flags = 0;

	hash->length = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(&desc.shash);
	if (rc != 0)
		return rc;

	i_size = i_size_read(file_inode(file));

	if (i_size == 0)
		goto out;

	rbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rbuf)
		return -ENOMEM;

	if (!(file->f_mode & FMODE_READ)) {
		file->f_mode |= FMODE_READ;
		read = 1;
	}

	while (offset < i_size) {
		int rbuf_len;

		rbuf_len = ima_kernel_read(file, offset, rbuf, PAGE_SIZE);
		if (rbuf_len < 0) {
			rc = rbuf_len;
			break;
		}
		if (rbuf_len == 0)
			break;
		offset += rbuf_len;

		rc = crypto_shash_update(&desc.shash, rbuf, rbuf_len);
		if (rc)
			break;
	}
	if (read)
		file->f_mode &= ~FMODE_READ;
	kfree(rbuf);
out:
	if (!rc)
		rc = crypto_shash_final(&desc.shash, hash->digest);
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

	i_size = i_size_read(file_inode(file));

	if (ima_ahash_minsize && i_size >= ima_ahash_minsize) {
		rc = ima_calc_file_ahash(file, hash);
		if (!rc)
			return 0;
	}

	return ima_calc_file_shash(file, hash);
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
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(tfm)];
	} desc;
	int rc, i;

	desc.shash.tfm = tfm;
	desc.shash.flags = 0;

	hash->length = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(&desc.shash);
	if (rc != 0)
		return rc;

	for (i = 0; i < num_fields; i++) {
		u8 buffer[IMA_EVENT_NAME_LEN_MAX + 1] = { 0 };
		u8 *data_to_hash = field_data[i].data;
		u32 datalen = field_data[i].len;

		if (strcmp(td->name, IMA_TEMPLATE_IMA_NAME) != 0) {
			rc = crypto_shash_update(&desc.shash,
						(const u8 *) &field_data[i].len,
						sizeof(field_data[i].len));
			if (rc)
				break;
		} else if (strcmp(td->fields[i]->field_id, "n") == 0) {
			memcpy(buffer, data_to_hash, datalen);
			data_to_hash = buffer;
			datalen = IMA_EVENT_NAME_LEN_MAX + 1;
		}
		rc = crypto_shash_update(&desc.shash, data_to_hash, datalen);
		if (rc)
			break;
	}

	if (!rc)
		rc = crypto_shash_final(&desc.shash, hash->digest);

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

static void __init ima_pcrread(int idx, u8 *pcr)
{
	if (!ima_used_chip)
		return;

	if (tpm_pcr_read(TPM_ANY_NUM, idx, pcr) != 0)
		pr_err("Error Communicating to TPM chip\n");
}

/*
 * Calculate the boot aggregate hash
 */
static int __init ima_calc_boot_aggregate_tfm(char *digest,
					      struct crypto_shash *tfm)
{
	u8 pcr_i[TPM_DIGEST_SIZE];
	int rc, i;
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(tfm)];
	} desc;

	desc.shash.tfm = tfm;
	desc.shash.flags = 0;

	rc = crypto_shash_init(&desc.shash);
	if (rc != 0)
		return rc;

	/* cumulative sha1 over tpm registers 0-7 */
	for (i = TPM_PCR0; i < TPM_PCR8; i++) {
		ima_pcrread(i, pcr_i);
		/* now accumulate with current aggregate */
		rc = crypto_shash_update(&desc.shash, pcr_i, TPM_DIGEST_SIZE);
	}
	if (!rc)
		crypto_shash_final(&desc.shash, digest);
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
