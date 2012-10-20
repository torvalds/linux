/* Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <crypto/public_key.h>
#include <crypto/hash.h>
#include <keys/asymmetric-type.h>
#include "module-internal.h"

/*
 * Module signature information block.
 *
 * The constituents of the signature section are, in order:
 *
 *	- Signer's name
 *	- Key identifier
 *	- Signature data
 *	- Information block
 */
struct module_signature {
	enum pkey_algo		algo : 8;	/* Public-key crypto algorithm */
	enum pkey_hash_algo	hash : 8;	/* Digest algorithm */
	enum pkey_id_type	id_type : 8;	/* Key identifier type */
	u8			signer_len;	/* Length of signer's name */
	u8			key_id_len;	/* Length of key identifier */
	u8			__pad[3];
	__be32			sig_len;	/* Length of signature data */
};

/*
 * Digest the module contents.
 */
static struct public_key_signature *mod_make_digest(enum pkey_hash_algo hash,
						    const void *mod,
						    unsigned long modlen)
{
	struct public_key_signature *pks;
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	int ret;

	pr_devel("==>%s()\n", __func__);
	
	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(pkey_hash_algo[hash], 0, 0);
	if (IS_ERR(tfm))
		return (PTR_ERR(tfm) == -ENOENT) ? ERR_PTR(-ENOPKG) : ERR_CAST(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);

	/* We allocate the hash operational data storage on the end of our
	 * context data and the digest output buffer on the end of that.
	 */
	ret = -ENOMEM;
	pks = kzalloc(digest_size + sizeof(*pks) + desc_size, GFP_KERNEL);
	if (!pks)
		goto error_no_pks;

	pks->pkey_hash_algo	= hash;
	pks->digest		= (u8 *)pks + sizeof(*pks) + desc_size;
	pks->digest_size	= digest_size;

	desc = (void *)pks + sizeof(*pks);
	desc->tfm   = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;

	ret = crypto_shash_finup(desc, mod, modlen, pks->digest);
	if (ret < 0)
		goto error;

	crypto_free_shash(tfm);
	pr_devel("<==%s() = ok\n", __func__);
	return pks;

error:
	kfree(pks);
error_no_pks:
	crypto_free_shash(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ERR_PTR(ret);
}

/*
 * Extract an MPI array from the signature data.  This represents the actual
 * signature.  Each raw MPI is prefaced by a BE 2-byte value indicating the
 * size of the MPI in bytes.
 *
 * RSA signatures only have one MPI, so currently we only read one.
 */
static int mod_extract_mpi_array(struct public_key_signature *pks,
				 const void *data, size_t len)
{
	size_t nbytes;
	MPI mpi;

	if (len < 3)
		return -EBADMSG;
	nbytes = ((const u8 *)data)[0] << 8 | ((const u8 *)data)[1];
	data += 2;
	len -= 2;
	if (len != nbytes)
		return -EBADMSG;

	mpi = mpi_read_raw_data(data, nbytes);
	if (!mpi)
		return -ENOMEM;
	pks->mpi[0] = mpi;
	pks->nr_mpi = 1;
	return 0;
}

/*
 * Request an asymmetric key.
 */
static struct key *request_asymmetric_key(const char *signer, size_t signer_len,
					  const u8 *key_id, size_t key_id_len)
{
	key_ref_t key;
	size_t i;
	char *id, *q;

	pr_devel("==>%s(,%zu,,%zu)\n", __func__, signer_len, key_id_len);

	/* Construct an identifier. */
	id = kmalloc(signer_len + 2 + key_id_len * 2 + 1, GFP_KERNEL);
	if (!id)
		return ERR_PTR(-ENOKEY);

	memcpy(id, signer, signer_len);

	q = id + signer_len;
	*q++ = ':';
	*q++ = ' ';
	for (i = 0; i < key_id_len; i++) {
		*q++ = hex_asc[*key_id >> 4];
		*q++ = hex_asc[*key_id++ & 0x0f];
	}

	*q = 0;

	pr_debug("Look up: \"%s\"\n", id);

	key = keyring_search(make_key_ref(modsign_keyring, 1),
			     &key_type_asymmetric, id);
	if (IS_ERR(key))
		pr_warn("Request for unknown module key '%s' err %ld\n",
			id, PTR_ERR(key));
	kfree(id);

	if (IS_ERR(key)) {
		switch (PTR_ERR(key)) {
			/* Hide some search errors */
		case -EACCES:
		case -ENOTDIR:
		case -EAGAIN:
			return ERR_PTR(-ENOKEY);
		default:
			return ERR_CAST(key);
		}
	}

	pr_devel("<==%s() = 0 [%x]\n", __func__, key_serial(key_ref_to_ptr(key)));
	return key_ref_to_ptr(key);
}

/*
 * Verify the signature on a module.
 */
int mod_verify_sig(const void *mod, unsigned long *_modlen)
{
	struct public_key_signature *pks;
	struct module_signature ms;
	struct key *key;
	const void *sig;
	size_t modlen = *_modlen, sig_len;
	int ret;

	pr_devel("==>%s(,%lu)\n", __func__, modlen);

	if (modlen <= sizeof(ms))
		return -EBADMSG;

	memcpy(&ms, mod + (modlen - sizeof(ms)), sizeof(ms));
	modlen -= sizeof(ms);

	sig_len = be32_to_cpu(ms.sig_len);
	if (sig_len >= modlen)
		return -EBADMSG;
	modlen -= sig_len;
	if ((size_t)ms.signer_len + ms.key_id_len >= modlen)
		return -EBADMSG;
	modlen -= (size_t)ms.signer_len + ms.key_id_len;

	*_modlen = modlen;
	sig = mod + modlen;

	/* For the moment, only support RSA and X.509 identifiers */
	if (ms.algo != PKEY_ALGO_RSA ||
	    ms.id_type != PKEY_ID_X509)
		return -ENOPKG;

	if (ms.hash >= PKEY_HASH__LAST ||
	    !pkey_hash_algo[ms.hash])
		return -ENOPKG;

	key = request_asymmetric_key(sig, ms.signer_len,
				     sig + ms.signer_len, ms.key_id_len);
	if (IS_ERR(key))
		return PTR_ERR(key);

	pks = mod_make_digest(ms.hash, mod, modlen);
	if (IS_ERR(pks)) {
		ret = PTR_ERR(pks);
		goto error_put_key;
	}

	ret = mod_extract_mpi_array(pks, sig + ms.signer_len + ms.key_id_len,
				    sig_len);
	if (ret < 0)
		goto error_free_pks;

	ret = verify_signature(key, pks);
	pr_devel("verify_signature() = %d\n", ret);

error_free_pks:
	mpi_free(pks->rsa.s);
	kfree(pks);
error_put_key:
	key_put(key);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;	
}
