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

struct ima_algo_desc {
	struct crypto_shash *tfm;
	enum hash_algo algo;
};

int ima_sha1_idx __ro_after_init;
int ima_hash_algo_idx __ro_after_init;
/*
 * Additional number of slots reserved, as needed, for SHA1
 * and IMA default algo.
 */
int ima_extra_slots __ro_after_init;

static struct ima_algo_desc *ima_algo_array;

static int __init ima_init_ima_crypto(void)
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
	int rc, i;

	if (algo < 0 || algo >= HASH_ALGO__LAST)
		algo = ima_hash_algo;

	if (algo == ima_hash_algo)
		return tfm;

	for (i = 0; i < NR_BANKS(ima_tpm_chip) + ima_extra_slots; i++)
		if (ima_algo_array[i].tfm && ima_algo_array[i].algo == algo)
			return ima_algo_array[i].tfm;

	tfm = crypto_alloc_shash(hash_algo_name[algo], 0, 0);
	if (IS_ERR(tfm)) {
		rc = PTR_ERR(tfm);
		pr_err("Can not allocate %s (reason: %d)\n",
		       hash_algo_name[algo], rc);
	}
	return tfm;
}

int __init ima_init_crypto(void)
{
	enum hash_algo algo;
	long rc;
	int i;

	rc = ima_init_ima_crypto();
	if (rc)
		return rc;

	ima_sha1_idx = -1;
	ima_hash_algo_idx = -1;

	for (i = 0; i < NR_BANKS(ima_tpm_chip); i++) {
		algo = ima_tpm_chip->allocated_banks[i].crypto_id;
		if (algo == HASH_ALGO_SHA1)
			ima_sha1_idx = i;

		if (algo == ima_hash_algo)
			ima_hash_algo_idx = i;
	}

	if (ima_sha1_idx < 0) {
		ima_sha1_idx = NR_BANKS(ima_tpm_chip) + ima_extra_slots++;
		if (ima_hash_algo == HASH_ALGO_SHA1)
			ima_hash_algo_idx = ima_sha1_idx;
	}

	if (ima_hash_algo_idx < 0)
		ima_hash_algo_idx = NR_BANKS(ima_tpm_chip) + ima_extra_slots++;

	ima_algo_array = kcalloc(NR_BANKS(ima_tpm_chip) + ima_extra_slots,
				 sizeof(*ima_algo_array), GFP_KERNEL);
	if (!ima_algo_array) {
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < NR_BANKS(ima_tpm_chip); i++) {
		algo = ima_tpm_chip->allocated_banks[i].crypto_id;
		ima_algo_array[i].algo = algo;

		/* unknown TPM algorithm */
		if (algo == HASH_ALGO__LAST)
			continue;

		if (algo == ima_hash_algo) {
			ima_algo_array[i].tfm = ima_shash_tfm;
			continue;
		}

		ima_algo_array[i].tfm = ima_alloc_tfm(algo);
		if (IS_ERR(ima_algo_array[i].tfm)) {
			if (algo == HASH_ALGO_SHA1) {
				rc = PTR_ERR(ima_algo_array[i].tfm);
				ima_algo_array[i].tfm = NULL;
				goto out_array;
			}

			ima_algo_array[i].tfm = NULL;
		}
	}

	if (ima_sha1_idx >= NR_BANKS(ima_tpm_chip)) {
		if (ima_hash_algo == HASH_ALGO_SHA1) {
			ima_algo_array[ima_sha1_idx].tfm = ima_shash_tfm;
		} else {
			ima_algo_array[ima_sha1_idx].tfm =
						ima_alloc_tfm(HASH_ALGO_SHA1);
			if (IS_ERR(ima_algo_array[ima_sha1_idx].tfm)) {
				rc = PTR_ERR(ima_algo_array[ima_sha1_idx].tfm);
				goto out_array;
			}
		}

		ima_algo_array[ima_sha1_idx].algo = HASH_ALGO_SHA1;
	}

	if (ima_hash_algo_idx >= NR_BANKS(ima_tpm_chip) &&
	    ima_hash_algo_idx != ima_sha1_idx) {
		ima_algo_array[ima_hash_algo_idx].tfm = ima_shash_tfm;
		ima_algo_array[ima_hash_algo_idx].algo = ima_hash_algo;
	}

	return 0;
out_array:
	for (i = 0; i < NR_BANKS(ima_tpm_chip) + ima_extra_slots; i++) {
		if (!ima_algo_array[i].tfm ||
		    ima_algo_array[i].tfm == ima_shash_tfm)
			continue;

		crypto_free_shash(ima_algo_array[i].tfm);
	}
	kfree(ima_algo_array);
out:
	crypto_free_shash(ima_shash_tfm);
	return rc;
}

static void ima_free_tfm(struct crypto_shash *tfm)
{
	int i;

	if (tfm == ima_shash_tfm)
		return;

	for (i = 0; i < NR_BANKS(ima_tpm_chip) + ima_extra_slots; i++)
		if (ima_algo_array[i].tfm == tfm)
			return;

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
	bool new_file_instance = false;

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
		if (IS_ERR(f))
			return PTR_ERR(f);

		new_file_instance = true;
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
	return rc;
}

/*
 * Calculate the hash of template data
 */
static int ima_calc_field_array_hash_tfm(struct ima_field_data *field_data,
					 struct ima_template_entry *entry,
					 int tfm_idx)
{
	SHASH_DESC_ON_STACK(shash, ima_algo_array[tfm_idx].tfm);
	struct ima_template_desc *td = entry->template_desc;
	int num_fields = entry->template_desc->num_fields;
	int rc, i;

	shash->tfm = ima_algo_array[tfm_idx].tfm;

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
		rc = crypto_shash_final(shash, entry->digests[tfm_idx].digest);

	return rc;
}

int ima_calc_field_array_hash(struct ima_field_data *field_data,
			      struct ima_template_entry *entry)
{
	u16 alg_id;
	int rc, i;

	rc = ima_calc_field_array_hash_tfm(field_data, entry, ima_sha1_idx);
	if (rc)
		return rc;

	entry->digests[ima_sha1_idx].alg_id = TPM_ALG_SHA1;

	for (i = 0; i < NR_BANKS(ima_tpm_chip) + ima_extra_slots; i++) {
		if (i == ima_sha1_idx)
			continue;

		if (i < NR_BANKS(ima_tpm_chip)) {
			alg_id = ima_tpm_chip->allocated_banks[i].alg_id;
			entry->digests[i].alg_id = alg_id;
		}

		/* for unmapped TPM algorithms digest is still a padded SHA1 */
		if (!ima_algo_array[i].tfm) {
			memcpy(entry->digests[i].digest,
			       entry->digests[ima_sha1_idx].digest,
			       TPM_DIGEST_SIZE);
			continue;
		}

		rc = ima_calc_field_array_hash_tfm(field_data, entry, i);
		if (rc)
			return rc;
	}
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

static void ima_pcrread(u32 idx, struct tpm_digest *d)
{
	if (!ima_tpm_chip)
		return;

	if (tpm_pcr_read(ima_tpm_chip, idx, d) != 0)
		pr_err("Error Communicating to TPM chip\n");
}

/*
 * The boot_aggregate is a cumulative hash over TPM registers 0 - 7.  With
 * TPM 1.2 the boot_aggregate was based on reading the SHA1 PCRs, but with
 * TPM 2.0 hash agility, TPM chips could support multiple TPM PCR banks,
 * allowing firmware to configure and enable different banks.
 *
 * Knowing which TPM bank is read to calculate the boot_aggregate digest
 * needs to be conveyed to a verifier.  For this reason, use the same
 * hash algorithm for reading the TPM PCRs as for calculating the boot
 * aggregate digest as stored in the measurement list.
 */
static int ima_calc_boot_aggregate_tfm(char *digest, u16 alg_id,
				       struct crypto_shash *tfm)
{
	struct tpm_digest d = { .alg_id = alg_id, .digest = {0} };
	int rc;
	u32 i;
	SHASH_DESC_ON_STACK(shash, tfm);

	shash->tfm = tfm;

	pr_devel("calculating the boot-aggregate based on TPM bank: %04x\n",
		 d.alg_id);

	rc = crypto_shash_init(shash);
	if (rc != 0)
		return rc;

	/* cumulative digest over TPM registers 0-7 */
	for (i = TPM_PCR0; i < TPM_PCR8; i++) {
		ima_pcrread(i, &d);
		/* now accumulate with current aggregate */
		rc = crypto_shash_update(shash, d.digest,
					 crypto_shash_digestsize(tfm));
		if (rc != 0)
			return rc;
	}
	/*
	 * Extend cumulative digest over TPM registers 8-9, which contain
	 * measurement for the kernel command line (reg. 8) and image (reg. 9)
	 * in a typical PCR allocation. Registers 8-9 are only included in
	 * non-SHA1 boot_aggregate digests to avoid ambiguity.
	 */
	if (alg_id != TPM_ALG_SHA1) {
		for (i = TPM_PCR8; i < TPM_PCR10; i++) {
			ima_pcrread(i, &d);
			rc = crypto_shash_update(shash, d.digest,
						crypto_shash_digestsize(tfm));
		}
	}
	if (!rc)
		crypto_shash_final(shash, digest);
	return rc;
}

int ima_calc_boot_aggregate(struct ima_digest_data *hash)
{
	struct crypto_shash *tfm;
	u16 crypto_id, alg_id;
	int rc, i, bank_idx = -1;

	for (i = 0; i < ima_tpm_chip->nr_allocated_banks; i++) {
		crypto_id = ima_tpm_chip->allocated_banks[i].crypto_id;
		if (crypto_id == hash->algo) {
			bank_idx = i;
			break;
		}

		if (crypto_id == HASH_ALGO_SHA256)
			bank_idx = i;

		if (bank_idx == -1 && crypto_id == HASH_ALGO_SHA1)
			bank_idx = i;
	}

	if (bank_idx == -1) {
		pr_err("No suitable TPM algorithm for boot aggregate\n");
		return 0;
	}

	hash->algo = ima_tpm_chip->allocated_banks[bank_idx].crypto_id;

	tfm = ima_alloc_tfm(hash->algo);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	hash->length = crypto_shash_digestsize(tfm);
	alg_id = ima_tpm_chip->allocated_banks[bank_idx].alg_id;
	rc = ima_calc_boot_aggregate_tfm(hash->digest, alg_id, tfm);

	ima_free_tfm(tfm);

	return rc;
}
