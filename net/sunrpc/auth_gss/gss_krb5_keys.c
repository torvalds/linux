/*
 * COPYRIGHT (c) 2008
 * The Regents of the University of Michigan
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <linux/lcm.h>
#include <crypto/hash.h>
#include <kunit/visibility.h>

#include "gss_krb5_internal.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

/**
 * krb5_nfold - n-fold function
 * @inbits: number of bits in @in
 * @in: buffer containing input to fold
 * @outbits: number of bits in the output buffer
 * @out: buffer to hold the result
 *
 * This is the n-fold function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */
VISIBLE_IF_KUNIT
void krb5_nfold(u32 inbits, const u8 *in, u32 outbits, u8 *out)
{
	unsigned long ulcm;
	int byte, i, msbit;

	/* the code below is more readable if I make these bytes
	   instead of bits */

	inbits >>= 3;
	outbits >>= 3;

	/* first compute lcm(n,k) */
	ulcm = lcm(inbits, outbits);

	/* now do the real work */

	memset(out, 0, outbits);
	byte = 0;

	/* this will end up cycling through k lcm(k,n)/k times, which
	   is correct */
	for (i = ulcm-1; i >= 0; i--) {
		/* compute the msbit in k which gets added into this byte */
		msbit = (
			/* first, start with the msbit in the first,
			 * unrotated byte */
			 ((inbits << 3) - 1)
			 /* then, for each byte, shift to the right
			  * for each repetition */
			 + (((inbits << 3) + 13) * (i/inbits))
			 /* last, pick out the correct byte within
			  * that shifted repetition */
			 + ((inbits - (i % inbits)) << 3)
			 ) % (inbits << 3);

		/* pull out the byte value itself */
		byte += (((in[((inbits - 1) - (msbit >> 3)) % inbits] << 8)|
				  (in[((inbits) - (msbit >> 3)) % inbits]))
				 >> ((msbit & 7) + 1)) & 0xff;

		/* do the addition */
		byte += out[i % outbits];
		out[i % outbits] = byte & 0xff;

		/* keep around the carry bit, if any */
		byte >>= 8;

	}

	/* if there's a carry bit left over, add it back in */
	if (byte) {
		for (i = outbits - 1; i >= 0; i--) {
			/* do the addition */
			byte += out[i];
			out[i] = byte & 0xff;

			/* keep around the carry bit, if any */
			byte >>= 8;
		}
	}
}
EXPORT_SYMBOL_IF_KUNIT(krb5_nfold);

/*
 * This is the DK (derive_key) function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */
static int krb5_DK(const struct gss_krb5_enctype *gk5e,
		   const struct xdr_netobj *inkey, u8 *rawkey,
		   const struct xdr_netobj *in_constant, gfp_t gfp_mask)
{
	size_t blocksize, keybytes, keylength, n;
	unsigned char *inblockdata, *outblockdata;
	struct xdr_netobj inblock, outblock;
	struct crypto_sync_skcipher *cipher;
	int ret = -EINVAL;

	keybytes = gk5e->keybytes;
	keylength = gk5e->keylength;

	if (inkey->len != keylength)
		goto err_return;

	cipher = crypto_alloc_sync_skcipher(gk5e->encrypt_name, 0, 0);
	if (IS_ERR(cipher))
		goto err_return;
	blocksize = crypto_sync_skcipher_blocksize(cipher);
	if (crypto_sync_skcipher_setkey(cipher, inkey->data, inkey->len))
		goto err_return;

	ret = -ENOMEM;
	inblockdata = kmalloc(blocksize, gfp_mask);
	if (inblockdata == NULL)
		goto err_free_cipher;

	outblockdata = kmalloc(blocksize, gfp_mask);
	if (outblockdata == NULL)
		goto err_free_in;

	inblock.data = (char *) inblockdata;
	inblock.len = blocksize;

	outblock.data = (char *) outblockdata;
	outblock.len = blocksize;

	/* initialize the input block */

	if (in_constant->len == inblock.len) {
		memcpy(inblock.data, in_constant->data, inblock.len);
	} else {
		krb5_nfold(in_constant->len * 8, in_constant->data,
			   inblock.len * 8, inblock.data);
	}

	/* loop encrypting the blocks until enough key bytes are generated */

	n = 0;
	while (n < keybytes) {
		krb5_encrypt(cipher, NULL, inblock.data, outblock.data,
			     inblock.len);

		if ((keybytes - n) <= outblock.len) {
			memcpy(rawkey + n, outblock.data, (keybytes - n));
			break;
		}

		memcpy(rawkey + n, outblock.data, outblock.len);
		memcpy(inblock.data, outblock.data, outblock.len);
		n += outblock.len;
	}

	ret = 0;

	kfree_sensitive(outblockdata);
err_free_in:
	kfree_sensitive(inblockdata);
err_free_cipher:
	crypto_free_sync_skcipher(cipher);
err_return:
	return ret;
}

/*
 * This is the identity function, with some sanity checking.
 */
static int krb5_random_to_key_v2(const struct gss_krb5_enctype *gk5e,
				 struct xdr_netobj *randombits,
				 struct xdr_netobj *key)
{
	int ret = -EINVAL;

	if (key->len != 16 && key->len != 32) {
		dprintk("%s: key->len is %d\n", __func__, key->len);
		goto err_out;
	}
	if (randombits->len != 16 && randombits->len != 32) {
		dprintk("%s: randombits->len is %d\n",
			__func__, randombits->len);
		goto err_out;
	}
	if (randombits->len != key->len) {
		dprintk("%s: randombits->len is %d, key->len is %d\n",
			__func__, randombits->len, key->len);
		goto err_out;
	}
	memcpy(key->data, randombits->data, key->len);
	ret = 0;
err_out:
	return ret;
}

/**
 * krb5_derive_key_v2 - Derive a subkey for an RFC 3962 enctype
 * @gk5e: Kerberos 5 enctype profile
 * @inkey: base protocol key
 * @outkey: OUT: derived key
 * @label: subkey usage label
 * @gfp_mask: memory allocation control flags
 *
 * Caller sets @outkey->len to the desired length of the derived key.
 *
 * On success, returns 0 and fills in @outkey. A negative errno value
 * is returned on failure.
 */
int krb5_derive_key_v2(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *label,
		       gfp_t gfp_mask)
{
	struct xdr_netobj inblock;
	int ret;

	inblock.len = gk5e->keybytes;
	inblock.data = kmalloc(inblock.len, gfp_mask);
	if (!inblock.data)
		return -ENOMEM;

	ret = krb5_DK(gk5e, inkey, inblock.data, label, gfp_mask);
	if (!ret)
		ret = krb5_random_to_key_v2(gk5e, &inblock, outkey);

	kfree_sensitive(inblock.data);
	return ret;
}

/*
 * K(i) = CMAC(key, K(i-1) | i | constant | 0x00 | k)
 *
 *    i: A block counter is used with a length of 4 bytes, represented
 *       in big-endian order.
 *
 *    constant: The label input to the KDF is the usage constant supplied
 *              to the key derivation function
 *
 *    k: The length of the output key in bits, represented as a 4-byte
 *       string in big-endian order.
 *
 * Caller fills in K(i-1) in @step, and receives the result K(i)
 * in the same buffer.
 */
static int
krb5_cmac_Ki(struct crypto_shash *tfm, const struct xdr_netobj *constant,
	     u32 outlen, u32 count, struct xdr_netobj *step)
{
	__be32 k = cpu_to_be32(outlen * 8);
	SHASH_DESC_ON_STACK(desc, tfm);
	__be32 i = cpu_to_be32(count);
	u8 zero = 0;
	int ret;

	desc->tfm = tfm;
	ret = crypto_shash_init(desc);
	if (ret)
		goto out_err;

	ret = crypto_shash_update(desc, step->data, step->len);
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, (u8 *)&i, sizeof(i));
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, constant->data, constant->len);
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, &zero, sizeof(zero));
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, (u8 *)&k, sizeof(k));
	if (ret)
		goto out_err;
	ret = crypto_shash_final(desc, step->data);
	if (ret)
		goto out_err;

out_err:
	shash_desc_zero(desc);
	return ret;
}

/**
 * krb5_kdf_feedback_cmac - Derive a subkey for a Camellia/CMAC-based enctype
 * @gk5e: Kerberos 5 enctype parameters
 * @inkey: base protocol key
 * @outkey: OUT: derived key
 * @constant: subkey usage label
 * @gfp_mask: memory allocation control flags
 *
 * RFC 6803 Section 3:
 *
 * "We use a key derivation function from the family specified in
 *  [SP800-108], Section 5.2, 'KDF in Feedback Mode'."
 *
 *	n = ceiling(k / 128)
 *	K(0) = zeros
 *	K(i) = CMAC(key, K(i-1) | i | constant | 0x00 | k)
 *	DR(key, constant) = k-truncate(K(1) | K(2) | ... | K(n))
 *	KDF-FEEDBACK-CMAC(key, constant) = random-to-key(DR(key, constant))
 *
 * Caller sets @outkey->len to the desired length of the derived key (k).
 *
 * On success, returns 0 and fills in @outkey. A negative errno value
 * is returned on failure.
 */
int
krb5_kdf_feedback_cmac(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *constant,
		       gfp_t gfp_mask)
{
	struct xdr_netobj step = { .data = NULL };
	struct xdr_netobj DR = { .data = NULL };
	unsigned int blocksize, offset;
	struct crypto_shash *tfm;
	int n, count, ret;

	/*
	 * This implementation assumes the CMAC used for an enctype's
	 * key derivation is the same as the CMAC used for its
	 * checksumming. This happens to be true for enctypes that
	 * are currently supported by this implementation.
	 */
	tfm = crypto_alloc_shash(gk5e->cksum_name, 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out;
	}
	ret = crypto_shash_setkey(tfm, inkey->data, inkey->len);
	if (ret)
		goto out_free_tfm;

	blocksize = crypto_shash_digestsize(tfm);
	n = (outkey->len + blocksize - 1) / blocksize;

	/* K(0) is all zeroes */
	ret = -ENOMEM;
	step.len = blocksize;
	step.data = kzalloc(step.len, gfp_mask);
	if (!step.data)
		goto out_free_tfm;

	DR.len = blocksize * n;
	DR.data = kmalloc(DR.len, gfp_mask);
	if (!DR.data)
		goto out_free_tfm;

	/* XXX: Does not handle partial-block key sizes */
	for (offset = 0, count = 1; count <= n; count++) {
		ret = krb5_cmac_Ki(tfm, constant, outkey->len, count, &step);
		if (ret)
			goto out_free_tfm;

		memcpy(DR.data + offset, step.data, blocksize);
		offset += blocksize;
	}

	/* k-truncate and random-to-key */
	memcpy(outkey->data, DR.data, outkey->len);
	ret = 0;

out_free_tfm:
	crypto_free_shash(tfm);
out:
	kfree_sensitive(step.data);
	kfree_sensitive(DR.data);
	return ret;
}

/*
 * K1 = HMAC-SHA(key, 0x00000001 | label | 0x00 | k)
 *
 *    key: The source of entropy from which subsequent keys are derived.
 *
 *    label: An octet string describing the intended usage of the
 *    derived key.
 *
 *    k: Length in bits of the key to be outputted, expressed in
 *    big-endian binary representation in 4 bytes.
 */
static int
krb5_hmac_K1(struct crypto_shash *tfm, const struct xdr_netobj *label,
	     u32 outlen, struct xdr_netobj *K1)
{
	__be32 k = cpu_to_be32(outlen * 8);
	SHASH_DESC_ON_STACK(desc, tfm);
	__be32 one = cpu_to_be32(1);
	u8 zero = 0;
	int ret;

	desc->tfm = tfm;
	ret = crypto_shash_init(desc);
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, (u8 *)&one, sizeof(one));
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, label->data, label->len);
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, &zero, sizeof(zero));
	if (ret)
		goto out_err;
	ret = crypto_shash_update(desc, (u8 *)&k, sizeof(k));
	if (ret)
		goto out_err;
	ret = crypto_shash_final(desc, K1->data);
	if (ret)
		goto out_err;

out_err:
	shash_desc_zero(desc);
	return ret;
}

/**
 * krb5_kdf_hmac_sha2 - Derive a subkey for an AES/SHA2-based enctype
 * @gk5e: Kerberos 5 enctype policy parameters
 * @inkey: base protocol key
 * @outkey: OUT: derived key
 * @label: subkey usage label
 * @gfp_mask: memory allocation control flags
 *
 * RFC 8009 Section 3:
 *
 *  "We use a key derivation function from Section 5.1 of [SP800-108],
 *   which uses the HMAC algorithm as the PRF."
 *
 *	function KDF-HMAC-SHA2(key, label, [context,] k):
 *		k-truncate(K1)
 *
 * Caller sets @outkey->len to the desired length of the derived key.
 *
 * On success, returns 0 and fills in @outkey. A negative errno value
 * is returned on failure.
 */
int
krb5_kdf_hmac_sha2(const struct gss_krb5_enctype *gk5e,
		   const struct xdr_netobj *inkey,
		   struct xdr_netobj *outkey,
		   const struct xdr_netobj *label,
		   gfp_t gfp_mask)
{
	struct crypto_shash *tfm;
	struct xdr_netobj K1 = {
		.data = NULL,
	};
	int ret;

	/*
	 * This implementation assumes the HMAC used for an enctype's
	 * key derivation is the same as the HMAC used for its
	 * checksumming. This happens to be true for enctypes that
	 * are currently supported by this implementation.
	 */
	tfm = crypto_alloc_shash(gk5e->cksum_name, 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out;
	}
	ret = crypto_shash_setkey(tfm, inkey->data, inkey->len);
	if (ret)
		goto out_free_tfm;

	K1.len = crypto_shash_digestsize(tfm);
	K1.data = kmalloc(K1.len, gfp_mask);
	if (!K1.data) {
		ret = -ENOMEM;
		goto out_free_tfm;
	}

	ret = krb5_hmac_K1(tfm, label, outkey->len, &K1);
	if (ret)
		goto out_free_tfm;

	/* k-truncate and random-to-key */
	memcpy(outkey->data, K1.data, outkey->len);

out_free_tfm:
	kfree_sensitive(K1.data);
	crypto_free_shash(tfm);
out:
	return ret;
}
