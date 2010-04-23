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
#include "ima.h"

static int init_desc(struct hash_desc *desc)
{
	int rc;

	desc->tfm = crypto_alloc_hash(ima_hash, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc->tfm)) {
		pr_info("failed to load %s transform: %ld\n",
			ima_hash, PTR_ERR(desc->tfm));
		rc = PTR_ERR(desc->tfm);
		return rc;
	}
	desc->flags = 0;
	rc = crypto_hash_init(desc);
	if (rc)
		crypto_free_hash(desc->tfm);
	return rc;
}

/*
 * Calculate the MD5/SHA1 file digest
 */
int ima_calc_hash(struct file *file, char *digest)
{
	struct hash_desc desc;
	struct scatterlist sg[1];
	loff_t i_size, offset = 0;
	char *rbuf;
	int rc;

	rc = init_desc(&desc);
	if (rc != 0)
		return rc;

	rbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rbuf) {
		rc = -ENOMEM;
		goto out;
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
		sg_init_one(sg, rbuf, rbuf_len);

		rc = crypto_hash_update(&desc, sg, rbuf_len);
		if (rc)
			break;
	}
	kfree(rbuf);
	if (!rc)
		rc = crypto_hash_final(&desc, digest);
out:
	crypto_free_hash(desc.tfm);
	return rc;
}

/*
 * Calculate the hash of a given template
 */
int ima_calc_template_hash(int template_len, void *template, char *digest)
{
	struct hash_desc desc;
	struct scatterlist sg[1];
	int rc;

	rc = init_desc(&desc);
	if (rc != 0)
		return rc;

	sg_init_one(sg, template, template_len);
	rc = crypto_hash_update(&desc, sg, template_len);
	if (!rc)
		rc = crypto_hash_final(&desc, digest);
	crypto_free_hash(desc.tfm);
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
int __init ima_calc_boot_aggregate(char *digest)
{
	struct hash_desc desc;
	struct scatterlist sg;
	u8 pcr_i[IMA_DIGEST_SIZE];
	int rc, i;

	rc = init_desc(&desc);
	if (rc != 0)
		return rc;

	/* cumulative sha1 over tpm registers 0-7 */
	for (i = TPM_PCR0; i < TPM_PCR8; i++) {
		ima_pcrread(i, pcr_i);
		/* now accumulate with current aggregate */
		sg_init_one(&sg, pcr_i, IMA_DIGEST_SIZE);
		rc = crypto_hash_update(&desc, &sg, IMA_DIGEST_SIZE);
	}
	if (!rc)
		crypto_hash_final(&desc, digest);
	crypto_free_hash(desc.tfm);
	return rc;
}
