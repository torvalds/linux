// SPDX-License-Identifier: GPL-2.0
/*
 * fs/verity/hash_algs.c: fs-verity hash algorithms
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <crypto/hash.h>
#include <linux/scatterlist.h>

/* The hash algorithms supported by fs-verity */
struct fsverity_hash_alg fsverity_hash_algs[] = {
	[FS_VERITY_HASH_ALG_SHA256] = {
		.name = "sha256",
		.digest_size = SHA256_DIGEST_SIZE,
		.block_size = SHA256_BLOCK_SIZE,
	},
	[FS_VERITY_HASH_ALG_SHA512] = {
		.name = "sha512",
		.digest_size = SHA512_DIGEST_SIZE,
		.block_size = SHA512_BLOCK_SIZE,
	},
};

/**
 * fsverity_get_hash_alg() - validate and prepare a hash algorithm
 * @inode: optional inode for logging purposes
 * @num: the hash algorithm number
 *
 * Get the struct fsverity_hash_alg for the given hash algorithm number, and
 * ensure it has a hash transform ready to go.  The hash transforms are
 * allocated on-demand so that we don't waste resources unnecessarily, and
 * because the crypto modules may be initialized later than fs/verity/.
 *
 * Return: pointer to the hash alg on success, else an ERR_PTR()
 */
const struct fsverity_hash_alg *fsverity_get_hash_alg(const struct inode *inode,
						      unsigned int num)
{
	struct fsverity_hash_alg *alg;
	struct crypto_ahash *tfm;
	int err;

	if (num >= ARRAY_SIZE(fsverity_hash_algs) ||
	    !fsverity_hash_algs[num].name) {
		fsverity_warn(inode, "Unknown hash algorithm number: %u", num);
		return ERR_PTR(-EINVAL);
	}
	alg = &fsverity_hash_algs[num];

	/* pairs with cmpxchg() below */
	tfm = READ_ONCE(alg->tfm);
	if (likely(tfm != NULL))
		return alg;
	/*
	 * Using the shash API would make things a bit simpler, but the ahash
	 * API is preferable as it allows the use of crypto accelerators.
	 */
	tfm = crypto_alloc_ahash(alg->name, 0, 0);
	if (IS_ERR(tfm)) {
		if (PTR_ERR(tfm) == -ENOENT) {
			fsverity_warn(inode,
				      "Missing crypto API support for hash algorithm \"%s\"",
				      alg->name);
			return ERR_PTR(-ENOPKG);
		}
		fsverity_err(inode,
			     "Error allocating hash algorithm \"%s\": %ld",
			     alg->name, PTR_ERR(tfm));
		return ERR_CAST(tfm);
	}

	err = -EINVAL;
	if (WARN_ON(alg->digest_size != crypto_ahash_digestsize(tfm)))
		goto err_free_tfm;
	if (WARN_ON(alg->block_size != crypto_ahash_blocksize(tfm)))
		goto err_free_tfm;

	pr_info("%s using implementation \"%s\"\n",
		alg->name, crypto_ahash_driver_name(tfm));

	/* pairs with READ_ONCE() above */
	if (cmpxchg(&alg->tfm, NULL, tfm) != NULL)
		crypto_free_ahash(tfm);

	return alg;

err_free_tfm:
	crypto_free_ahash(tfm);
	return ERR_PTR(err);
}

/**
 * fsverity_prepare_hash_state() - precompute the initial hash state
 * @alg: hash algorithm
 * @salt: a salt which is to be prepended to all data to be hashed
 * @salt_size: salt size in bytes, possibly 0
 *
 * Return: NULL if the salt is empty, otherwise the kmalloc()'ed precomputed
 *	   initial hash state on success or an ERR_PTR() on failure.
 */
const u8 *fsverity_prepare_hash_state(const struct fsverity_hash_alg *alg,
				      const u8 *salt, size_t salt_size)
{
	u8 *hashstate = NULL;
	struct ahash_request *req = NULL;
	u8 *padded_salt = NULL;
	size_t padded_salt_size;
	struct scatterlist sg;
	DECLARE_CRYPTO_WAIT(wait);
	int err;

	if (salt_size == 0)
		return NULL;

	hashstate = kmalloc(crypto_ahash_statesize(alg->tfm), GFP_KERNEL);
	if (!hashstate)
		return ERR_PTR(-ENOMEM);

	req = ahash_request_alloc(alg->tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto err_free;
	}

	/*
	 * Zero-pad the salt to the next multiple of the input size of the hash
	 * algorithm's compression function, e.g. 64 bytes for SHA-256 or 128
	 * bytes for SHA-512.  This ensures that the hash algorithm won't have
	 * any bytes buffered internally after processing the salt, thus making
	 * salted hashing just as fast as unsalted hashing.
	 */
	padded_salt_size = round_up(salt_size, alg->block_size);
	padded_salt = kzalloc(padded_salt_size, GFP_KERNEL);
	if (!padded_salt) {
		err = -ENOMEM;
		goto err_free;
	}
	memcpy(padded_salt, salt, salt_size);

	sg_init_one(&sg, padded_salt, padded_salt_size);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP |
					CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	ahash_request_set_crypt(req, &sg, NULL, padded_salt_size);

	err = crypto_wait_req(crypto_ahash_init(req), &wait);
	if (err)
		goto err_free;

	err = crypto_wait_req(crypto_ahash_update(req), &wait);
	if (err)
		goto err_free;

	err = crypto_ahash_export(req, hashstate);
	if (err)
		goto err_free;
out:
	ahash_request_free(req);
	kfree(padded_salt);
	return hashstate;

err_free:
	kfree(hashstate);
	hashstate = ERR_PTR(err);
	goto out;
}

/**
 * fsverity_hash_page() - hash a single data or hash page
 * @params: the Merkle tree's parameters
 * @inode: inode for which the hashing is being done
 * @req: preallocated hash request
 * @page: the page to hash
 * @out: output digest, size 'params->digest_size' bytes
 *
 * Hash a single data or hash block, assuming block_size == PAGE_SIZE.
 * The hash is salted if a salt is specified in the Merkle tree parameters.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_hash_page(const struct merkle_tree_params *params,
		       const struct inode *inode,
		       struct ahash_request *req, struct page *page, u8 *out)
{
	struct scatterlist sg;
	DECLARE_CRYPTO_WAIT(wait);
	int err;

	if (WARN_ON(params->block_size != PAGE_SIZE))
		return -EINVAL;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, page, PAGE_SIZE, 0);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP |
					CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	ahash_request_set_crypt(req, &sg, out, PAGE_SIZE);

	if (params->hashstate) {
		err = crypto_ahash_import(req, params->hashstate);
		if (err) {
			fsverity_err(inode,
				     "Error %d importing hash state", err);
			return err;
		}
		err = crypto_ahash_finup(req);
	} else {
		err = crypto_ahash_digest(req);
	}

	err = crypto_wait_req(err, &wait);
	if (err)
		fsverity_err(inode, "Error %d computing page hash", err);
	return err;
}

/**
 * fsverity_hash_buffer() - hash some data
 * @alg: the hash algorithm to use
 * @data: the data to hash
 * @size: size of data to hash, in bytes
 * @out: output digest, size 'alg->digest_size' bytes
 *
 * Hash some data which is located in physically contiguous memory (i.e. memory
 * allocated by kmalloc(), not by vmalloc()).  No salt is used.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_hash_buffer(const struct fsverity_hash_alg *alg,
			 const void *data, size_t size, u8 *out)
{
	struct ahash_request *req;
	struct scatterlist sg;
	DECLARE_CRYPTO_WAIT(wait);
	int err;

	req = ahash_request_alloc(alg->tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	sg_init_one(&sg, data, size);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP |
					CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	ahash_request_set_crypt(req, &sg, out, size);

	err = crypto_wait_req(crypto_ahash_digest(req), &wait);

	ahash_request_free(req);
	return err;
}

void __init fsverity_check_hash_algs(void)
{
	size_t i;

	/*
	 * Sanity check the hash algorithms (could be a build-time check, but
	 * they're in an array)
	 */
	for (i = 0; i < ARRAY_SIZE(fsverity_hash_algs); i++) {
		const struct fsverity_hash_alg *alg = &fsverity_hash_algs[i];

		if (!alg->name)
			continue;

		BUG_ON(alg->digest_size > FS_VERITY_MAX_DIGEST_SIZE);

		/*
		 * For efficiency, the implementation currently assumes the
		 * digest and block sizes are powers of 2.  This limitation can
		 * be lifted if the code is updated to handle other values.
		 */
		BUG_ON(!is_power_of_2(alg->digest_size));
		BUG_ON(!is_power_of_2(alg->block_size));
	}
}
