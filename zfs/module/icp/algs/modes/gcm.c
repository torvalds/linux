/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(_KERNEL) && defined(__amd64)
#include <linux/simd_x86.h>

#define	KPREEMPT_DISABLE	kfpu_begin()
#define	KPREEMPT_ENABLE		kfpu_end()

#else
#define	KPREEMPT_DISABLE
#define	KPREEMPT_ENABLE
#endif	/* _KERNEL */

#include <sys/zfs_context.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/byteorder.h>

#ifdef __amd64

extern void gcm_mul_pclmulqdq(uint64_t *x_in, uint64_t *y, uint64_t *res);
static int intel_pclmulqdq_instruction_present(void);
#endif	/* __amd64 */

struct aes_block {
	uint64_t a;
	uint64_t b;
};


/*
 * gcm_mul()
 * Perform a carry-less multiplication (that is, use XOR instead of the
 * multiply operator) on *x_in and *y and place the result in *res.
 *
 * Byte swap the input (*x_in and *y) and the output (*res).
 *
 * Note: x_in, y, and res all point to 16-byte numbers (an array of two
 * 64-bit integers).
 */
void
gcm_mul(uint64_t *x_in, uint64_t *y, uint64_t *res)
{
#ifdef __amd64
	if (intel_pclmulqdq_instruction_present()) {
		KPREEMPT_DISABLE;
		gcm_mul_pclmulqdq(x_in, y, res);
		KPREEMPT_ENABLE;
	} else
#endif	/* __amd64 */
	{
		static const uint64_t R = 0xe100000000000000ULL;
		struct aes_block z = {0, 0};
		struct aes_block v;
		uint64_t x;
		int i, j;

		v.a = ntohll(y[0]);
		v.b = ntohll(y[1]);

		for (j = 0; j < 2; j++) {
			x = ntohll(x_in[j]);
			for (i = 0; i < 64; i++, x <<= 1) {
				if (x & 0x8000000000000000ULL) {
					z.a ^= v.a;
					z.b ^= v.b;
				}
				if (v.b & 1ULL) {
					v.b = (v.a << 63)|(v.b >> 1);
					v.a = (v.a >> 1) ^ R;
				} else {
					v.b = (v.a << 63)|(v.b >> 1);
					v.a = v.a >> 1;
				}
			}
		}
		res[0] = htonll(z.a);
		res[1] = htonll(z.b);
	}
}


#define	GHASH(c, d, t) \
	xor_block((uint8_t *)(d), (uint8_t *)(c)->gcm_ghash); \
	gcm_mul((uint64_t *)(void *)(c)->gcm_ghash, (c)->gcm_H, \
	(uint64_t *)(void *)(t));


/*
 * Encrypt multiple blocks of data in GCM mode.  Decrypt for GCM mode
 * is done in another function.
 */
int
gcm_mode_encrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	size_t need = 0;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *lastp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);

	if (length + ctx->gcm_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->gcm_remainder + ctx->gcm_remainder_len,
		    length);
		ctx->gcm_remainder_len += length;
		ctx->gcm_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	lastp = (uint8_t *)ctx->gcm_cb;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		/* Unprocessed data from last call. */
		if (ctx->gcm_remainder_len > 0) {
			need = block_size - ctx->gcm_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->gcm_remainder)
			    [ctx->gcm_remainder_len], need);

			blockp = (uint8_t *)ctx->gcm_remainder;
		} else {
			blockp = datap;
		}

		/*
		 * Increment counter. Counter bits are confined
		 * to the bottom 32 bits of the counter block.
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
		    (uint8_t *)ctx->gcm_tmp);
		xor_block(blockp, (uint8_t *)ctx->gcm_tmp);

		lastp = (uint8_t *)ctx->gcm_tmp;

		ctx->gcm_processed_data_len += block_size;

		if (out == NULL) {
			if (ctx->gcm_remainder_len > 0) {
				bcopy(blockp, ctx->gcm_copy_to,
				    ctx->gcm_remainder_len);
				bcopy(blockp + ctx->gcm_remainder_len, datap,
				    need);
			}
		} else {
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, block_size);

			/* copy block to where it belongs */
			if (out_data_1_len == block_size) {
				copy_block(lastp, out_data_1);
			} else {
				bcopy(lastp, out_data_1, out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(lastp + out_data_1_len,
					    out_data_2,
					    block_size - out_data_1_len);
				}
			}
			/* update offset */
			out->cd_offset += block_size;
		}

		/* add ciphertext to the hash */
		GHASH(ctx, ctx->gcm_tmp, ctx->gcm_ghash);

		/* Update pointer to next block of data to be processed. */
		if (ctx->gcm_remainder_len != 0) {
			datap += need;
			ctx->gcm_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->gcm_remainder, remainder);
			ctx->gcm_remainder_len = remainder;
			ctx->gcm_copy_to = datap;
			goto out;
		}
		ctx->gcm_copy_to = NULL;

	} while (remainder > 0);
out:
	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
int
gcm_encrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	uint8_t *ghash, *macp = NULL;
	int i, rv;

	if (out->cd_length <
	    (ctx->gcm_remainder_len + ctx->gcm_tag_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	ghash = (uint8_t *)ctx->gcm_ghash;

	if (ctx->gcm_remainder_len > 0) {
		uint64_t counter;
		uint8_t *tmpp = (uint8_t *)ctx->gcm_tmp;

		/*
		 * Here is where we deal with data that is not a
		 * multiple of the block size.
		 */

		/*
		 * Increment counter.
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
		    (uint8_t *)ctx->gcm_tmp);

		macp = (uint8_t *)ctx->gcm_remainder;
		bzero(macp + ctx->gcm_remainder_len,
		    block_size - ctx->gcm_remainder_len);

		/* XOR with counter block */
		for (i = 0; i < ctx->gcm_remainder_len; i++) {
			macp[i] ^= tmpp[i];
		}

		/* add ciphertext to the hash */
		GHASH(ctx, macp, ghash);

		ctx->gcm_processed_data_len += ctx->gcm_remainder_len;
	}

	ctx->gcm_len_a_len_c[1] =
	    htonll(CRYPTO_BYTES2BITS(ctx->gcm_processed_data_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	if (ctx->gcm_remainder_len > 0) {
		rv = crypto_put_output_data(macp, out, ctx->gcm_remainder_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}
	out->cd_offset += ctx->gcm_remainder_len;
	ctx->gcm_remainder_len = 0;
	rv = crypto_put_output_data(ghash, out, ctx->gcm_tag_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);
	out->cd_offset += ctx->gcm_tag_len;

	return (CRYPTO_SUCCESS);
}

/*
 * This will only deal with decrypting the last block of the input that
 * might not be a multiple of block length.
 */
static void
gcm_decrypt_incomplete_block(gcm_ctx_t *ctx, size_t block_size, size_t index,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *datap, *outp, *counterp;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	int i;

	/*
	 * Increment counter.
	 * Counter bits are confined to the bottom 32 bits
	 */
	counter = ntohll(ctx->gcm_cb[1] & counter_mask);
	counter = htonll(counter + 1);
	counter &= counter_mask;
	ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

	datap = (uint8_t *)ctx->gcm_remainder;
	outp = &((ctx->gcm_pt_buf)[index]);
	counterp = (uint8_t *)ctx->gcm_tmp;

	/* authentication tag */
	bzero((uint8_t *)ctx->gcm_tmp, block_size);
	bcopy(datap, (uint8_t *)ctx->gcm_tmp, ctx->gcm_remainder_len);

	/* add ciphertext to the hash */
	GHASH(ctx, ctx->gcm_tmp, ctx->gcm_ghash);

	/* decrypt remaining ciphertext */
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb, counterp);

	/* XOR with counter block */
	for (i = 0; i < ctx->gcm_remainder_len; i++) {
		outp[i] = datap[i] ^ counterp[i];
	}
}

/* ARGSUSED */
int
gcm_mode_decrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t new_len;
	uint8_t *new;

	/*
	 * Copy contiguous ciphertext input blocks to plaintext buffer.
	 * Ciphertext will be decrypted in the final.
	 */
	if (length > 0) {
		new_len = ctx->gcm_pt_buf_len + length;
		new = vmem_alloc(new_len, ctx->gcm_kmflag);
		bcopy(ctx->gcm_pt_buf, new, ctx->gcm_pt_buf_len);
		vmem_free(ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
		if (new == NULL)
			return (CRYPTO_HOST_MEMORY);

		ctx->gcm_pt_buf = new;
		ctx->gcm_pt_buf_len = new_len;
		bcopy(data, &ctx->gcm_pt_buf[ctx->gcm_processed_data_len],
		    length);
		ctx->gcm_processed_data_len += length;
	}

	ctx->gcm_remainder_len = 0;
	return (CRYPTO_SUCCESS);
}

int
gcm_decrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t pt_len;
	size_t remainder;
	uint8_t *ghash;
	uint8_t *blockp;
	uint8_t *cbp;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	int processed = 0, rv;

	ASSERT(ctx->gcm_processed_data_len == ctx->gcm_pt_buf_len);

	pt_len = ctx->gcm_processed_data_len - ctx->gcm_tag_len;
	ghash = (uint8_t *)ctx->gcm_ghash;
	blockp = ctx->gcm_pt_buf;
	remainder = pt_len;
	while (remainder > 0) {
		/* Incomplete last block */
		if (remainder < block_size) {
			bcopy(blockp, ctx->gcm_remainder, remainder);
			ctx->gcm_remainder_len = remainder;
			/*
			 * not expecting anymore ciphertext, just
			 * compute plaintext for the remaining input
			 */
			gcm_decrypt_incomplete_block(ctx, block_size,
			    processed, encrypt_block, xor_block);
			ctx->gcm_remainder_len = 0;
			goto out;
		}
		/* add ciphertext to the hash */
		GHASH(ctx, blockp, ghash);

		/*
		 * Increment counter.
		 * Counter bits are confined to the bottom 32 bits
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		cbp = (uint8_t *)ctx->gcm_tmp;
		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb, cbp);

		/* XOR with ciphertext */
		xor_block(cbp, blockp);

		processed += block_size;
		blockp += block_size;
		remainder -= block_size;
	}
out:
	ctx->gcm_len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(pt_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	/* compare the input authentication tag with what we calculated */
	if (bcmp(&ctx->gcm_pt_buf[pt_len], ghash, ctx->gcm_tag_len)) {
		/* They don't match */
		return (CRYPTO_INVALID_MAC);
	} else {
		rv = crypto_put_output_data(ctx->gcm_pt_buf, out, pt_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
		out->cd_offset += pt_len;
	}
	return (CRYPTO_SUCCESS);
}

static int
gcm_validate_args(CK_AES_GCM_PARAMS *gcm_param)
{
	size_t tag_len;

	/*
	 * Check the length of the authentication tag (in bits).
	 */
	tag_len = gcm_param->ulTagBits;
	switch (tag_len) {
	case 32:
	case 64:
	case 96:
	case 104:
	case 112:
	case 120:
	case 128:
		break;
	default:
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	if (gcm_param->ulIvLen == 0)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	return (CRYPTO_SUCCESS);
}

static void
gcm_format_initial_blocks(uchar_t *iv, ulong_t iv_len,
    gcm_ctx_t *ctx, size_t block_size,
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *cb;
	ulong_t remainder = iv_len;
	ulong_t processed = 0;
	uint8_t *datap, *ghash;
	uint64_t len_a_len_c[2];

	ghash = (uint8_t *)ctx->gcm_ghash;
	cb = (uint8_t *)ctx->gcm_cb;
	if (iv_len == 12) {
		bcopy(iv, cb, 12);
		cb[12] = 0;
		cb[13] = 0;
		cb[14] = 0;
		cb[15] = 1;
		/* J0 will be used again in the final */
		copy_block(cb, (uint8_t *)ctx->gcm_J0);
	} else {
		/* GHASH the IV */
		do {
			if (remainder < block_size) {
				bzero(cb, block_size);
				bcopy(&(iv[processed]), cb, remainder);
				datap = (uint8_t *)cb;
				remainder = 0;
			} else {
				datap = (uint8_t *)(&(iv[processed]));
				processed += block_size;
				remainder -= block_size;
			}
			GHASH(ctx, datap, ghash);
		} while (remainder > 0);

		len_a_len_c[0] = 0;
		len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(iv_len));
		GHASH(ctx, len_a_len_c, ctx->gcm_J0);

		/* J0 will be used again in the final */
		copy_block((uint8_t *)ctx->gcm_J0, (uint8_t *)cb);
	}
}

/*
 * The following function is called at encrypt or decrypt init time
 * for AES GCM mode.
 */
int
gcm_init(gcm_ctx_t *ctx, unsigned char *iv, size_t iv_len,
    unsigned char *auth_data, size_t auth_data_len, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *ghash, *datap, *authp;
	size_t remainder, processed;

	/* encrypt zero block to get subkey H */
	bzero(ctx->gcm_H, sizeof (ctx->gcm_H));
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_H,
	    (uint8_t *)ctx->gcm_H);

	gcm_format_initial_blocks(iv, iv_len, ctx, block_size,
	    copy_block, xor_block);

	authp = (uint8_t *)ctx->gcm_tmp;
	ghash = (uint8_t *)ctx->gcm_ghash;
	bzero(authp, block_size);
	bzero(ghash, block_size);

	processed = 0;
	remainder = auth_data_len;
	do {
		if (remainder < block_size) {
			/*
			 * There's not a block full of data, pad rest of
			 * buffer with zero
			 */
			bzero(authp, block_size);
			bcopy(&(auth_data[processed]), authp, remainder);
			datap = (uint8_t *)authp;
			remainder = 0;
		} else {
			datap = (uint8_t *)(&(auth_data[processed]));
			processed += block_size;
			remainder -= block_size;
		}

		/* add auth data to the hash */
		GHASH(ctx, datap, ghash);

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}

int
gcm_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	int rv;
	CK_AES_GCM_PARAMS *gcm_param;

	if (param != NULL) {
		gcm_param = (CK_AES_GCM_PARAMS *)(void *)param;

		if ((rv = gcm_validate_args(gcm_param)) != 0) {
			return (rv);
		}

		gcm_ctx->gcm_tag_len = gcm_param->ulTagBits;
		gcm_ctx->gcm_tag_len >>= 3;
		gcm_ctx->gcm_processed_data_len = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gcm_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GCM_MODE;
	} else {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto out;
	}

	if (gcm_init(gcm_ctx, gcm_param->pIv, gcm_param->ulIvLen,
	    gcm_param->pAAD, gcm_param->ulAADLen, block_size,
	    encrypt_block, copy_block, xor_block) != 0) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}
out:
	return (rv);
}

int
gmac_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	int rv;
	CK_AES_GMAC_PARAMS *gmac_param;

	if (param != NULL) {
		gmac_param = (CK_AES_GMAC_PARAMS *)(void *)param;

		gcm_ctx->gcm_tag_len = CRYPTO_BITS2BYTES(AES_GMAC_TAG_BITS);
		gcm_ctx->gcm_processed_data_len = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gmac_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GMAC_MODE;
	} else {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto out;
	}

	if (gcm_init(gcm_ctx, gmac_param->pIv, AES_GMAC_IV_LEN,
	    gmac_param->pAAD, gmac_param->ulAADLen, block_size,
	    encrypt_block, copy_block, xor_block) != 0) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
	}
out:
	return (rv);
}

void *
gcm_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = kmem_zalloc(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GCM_MODE;
	return (gcm_ctx);
}

void *
gmac_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = kmem_zalloc(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GMAC_MODE;
	return (gcm_ctx);
}

void
gcm_set_kmflag(gcm_ctx_t *ctx, int kmflag)
{
	ctx->gcm_kmflag = kmflag;
}


#ifdef __amd64

#define	INTEL_PCLMULQDQ_FLAG (1 << 1)

/*
 * Return 1 if executing on Intel with PCLMULQDQ instructions,
 * otherwise 0 (i.e., Intel without PCLMULQDQ or AMD64).
 * Cache the result, as the CPU can't change.
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * is_x86_featureset().
 */
static int
intel_pclmulqdq_instruction_present(void)
{
	static int cached_result = -1;
	unsigned eax, ebx, ecx, edx;
	unsigned func, subfunc;

	if (cached_result == -1) { /* first time */
		/* check for an intel cpu */
		func = 0;
		subfunc = 0;

		__asm__ __volatile__(
		    "cpuid"
		    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		    : "a"(func), "c"(subfunc));

		if (memcmp((char *)(&ebx), "Genu", 4) == 0 &&
		    memcmp((char *)(&edx), "ineI", 4) == 0 &&
		    memcmp((char *)(&ecx), "ntel", 4) == 0) {
			func = 1;
			subfunc = 0;

			/* check for aes-ni instruction set */
			__asm__ __volatile__(
			    "cpuid"
			    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
			    : "a"(func), "c"(subfunc));

			cached_result = !!(ecx & INTEL_PCLMULQDQ_FLAG);
		} else {
			cached_result = 0;
		}
	}

	return (cached_result);
}

#endif	/* __amd64 */
