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
 * 	Calculates md5/sha1 file hash, template hash, boot-aggreate hash
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <crypto/hash.h>
#include "ima.h"

static struct crypto_shash *ima_shash_tfm;

int ima_init_crypto(void)
{
	long rc;

	ima_shash_tfm = crypto_alloc_shash(ima_hash, 0, 0);
	if (IS_ERR(ima_shash_tfm)) {
		rc = PTR_ERR(ima_shash_tfm);
		pr_err("Can not allocate %s (reason: %ld)\n", ima_hash, rc);
		return rc;
	}
	return 0;
}

/*
 * Calculate the MD5/SHA1 file digest
 */
int ima_calc_hash(struct file *file, char *digest)
{
	loff_t i_size, offset = 0;
	char *rbuf;
	int rc, read = 0;
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(ima_shash_tfm)];
	} desc;

	desc.shash.tfm = ima_shash_tfm;
	desc.shash.flags = 0;

	rc = crypto_shash_init(&desc.shash);
	if (rc != 0)
		return rc;

	rbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rbuf) {
		rc = -ENOMEM;
		goto out;
	}
	if (!(file->f_mode & FMODE_READ)) {
		file->f_mode |= FMODE_READ;
		read = 1;
	}
	i_size = i_size_read(file->f_dentry->d_inode);
	while (offset < i_size) {
		int rbuf_len;

		rbuf_len = kernel_read(file, offset, rbuf, PAGE_SIZE);
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
	kfree(rbuf);
	if (!rc)
		rc = crypto_shash_final(&desc.shash, digest);
	if (read)
		file->f_mode &= ~FMODE_READ;
out:
	return rc;
}

/*
 * Calculate the hash of a given template
 */
int ima_calc_template_hash(int template_len, void *template, char *digest)
{
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(ima_shash_tfm)];
	} desc;

	desc.shash.tfm = ima_shash_tfm;
	desc.shash.flags = 0;

	return crypto_shash_digest(&desc.shash, template, template_len, digest);
}

static void __init ima_pcrread(int idx, u8 *pcr)
{
	if (!ima_used_chip)
		return;

	if (tpm_pcr_read(TPM_ANY_NUM, idx, pcr) != 0)
		pr_err("IMA: Error Communicating to TPM chip\n");
}

/*
 * Calculate the boot aggregate hash
 */
int __init ima_calc_boot_aggregate(char *digest)
{
	u8 pcr_i[IMA_DIGEST_SIZE];
	int rc, i;
	struct {
		struct shash_desc shash;
		char ctx[crypto_shash_descsize(ima_shash_tfm)];
	} desc;

	desc.shash.tfm = ima_shash_tfm;
	desc.shash.flags = 0;

	rc = crypto_shash_init(&desc.shash);
	if (rc != 0)
		return rc;

	/* cumulative sha1 over tpm registers 0-7 */
	for (i = TPM_PCR0; i < TPM_PCR8; i++) {
		ima_pcrread(i, pcr_i);
		/* now accumulate with current aggregate */
		rc = crypto_shash_update(&desc.shash, pcr_i, IMA_DIGEST_SIZE);
	}
	if (!rc)
		crypto_shash_final(&desc.shash, digest);
	return rc;
}
